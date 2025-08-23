#include "msg_scraper.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include <pthread.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "master_record.h"
#include "utils.h"

struct {
	u64snowflake* ids;
	size_t size;
} backup_guilds;
static int backup(struct discord* client, u64snowflake guild_id);
static void backup_timer(struct discord* client, struct discord_timer* timer)
{
	if(timer->flags & DISCORD_TIMER_TICK){
		fprintf(stderr, "Should backup delay %ld interval %ld\n", timer->delay, timer->interval);
		
		for(size_t i = 0; i < backup_guilds.size; ++i)
			backup(client, backup_guilds.ids[i]);
		
		master_record.last_backup_time = time(NULL);
		write_master_record();
	}
}
void msg_scraper_on_ready(struct discord* client, const struct discord_ready *e)
{
	struct discord_create_guild_application_command params = {
		.name = "backup",
		.description = "Initiate backup manually"
	};
	backup_guilds.size = e->guilds->size; backup_guilds.ids = malloc(e->guilds->size * sizeof(u64snowflake));
	for(int i = 0; i < e->guilds->size; ++i){
		discord_create_guild_application_command(client, e->application->id, e->guilds->array[i].id, &params, NULL);
		backup_guilds.ids[i] = e->guilds->array[i].id;
	}

	const time_t backup_interval = 10;
	time_t now = time(NULL);
	discord_timer_interval(client, backup_timer, backup_timer, NULL,
					(now > master_record.last_backup_time + backup_interval ? 0 :
					now > master_record.last_backup_time ? backup_interval - (now - master_record.last_backup_time) :
					backup_interval) * 1000,
					backup_interval * 1000, -1);
}

struct backup_got_messages_data
{
	char* guild_name;
	char* channel_name;
	FILE* msg_log;

	struct discord* client;
};
void _backup_got_messages(const struct discord_messages* msgs, void* _data)
{
	struct backup_got_messages_data* data = _data;

	if(msgs->size){
		for(int i = 0; i < msgs->size; ++i)
			fprintf(data->msg_log, "[%s]: %s\n", msgs->array[i].author->username, msgs->array[i].content);
	} else {
		fclose(data->msg_log);
		free(data->guild_name);
		free(data->channel_name);
		free(data);
	}
}

struct backup_dirent
{
	struct dirent* dent;
	unsigned long id;
};
static int backup_dirent_cmp(const void* _d1, const void* _d2)
{
	const struct backup_dirent *d1 = _d1, *d2 = _d2;
	if(d1->id < d2->id) return -1;
	return d1->id > d2->id;
}

static int backup(struct discord* client, u64snowflake guild_id)
{
	struct discord_guild guild = get_guild_by_id(client, guild_id);
	char backup_id_str[10];
	char* guild_backup_path = malloc(strlen("backup") + strlen(guild.name) + sizeof(backup_id_str) + 4);
	strcpy(guild_backup_path, "backup/");
	strcat(guild_backup_path, guild.name);
	make_dir(guild_backup_path, 0755);
	strcat(guild_backup_path, "/"); // goes after make_dir() to have it iterate 1 less time

	// scan directory for all subdirectories and sort them. Smallest ID = oldest backup
	struct dirent* dent;
	DIR* guild_backup_dir = opendir(guild_backup_path);
	size_t backups_ln = 10, backups_i = 0;
	struct backup_dirent* backups = malloc(sizeof(struct backup_dirent) * backups_ln);
	while((dent = readdir(guild_backup_dir))){
		if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
				(dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
			continue;

		struct stat st;
		fstatat(dirfd(guild_backup_dir), dent->d_name, &st, 0);
		if(S_ISDIR(st.st_mode)){
			char* endptr = NULL;
			unsigned long id = strtoul(dent->d_name, &endptr, 10);
			if(*endptr) // invalid ID (not a number)
				continue;
			if(backups_i >= backups_ln){
				backups_ln += 10;
				backups = realloc(backups, sizeof(struct backup_dirent) * backups_ln);
			}
			backups[backups_i++] = (struct backup_dirent){
				.dent = dent, .id = id
			};
		}
	}
	qsort(backups, backups_i, sizeof(struct backup_dirent), backup_dirent_cmp);

	// delete excessive backups
	char dirname_buf[4096];
	strcpy(dirname_buf, guild_backup_path);
	size_t dirname_buf_end = strlen(dirname_buf);
	const size_t MAX_BACKUPS = 3;
	if(backups_i >= MAX_BACKUPS){
		for(size_t i = 0; i < backups_i - MAX_BACKUPS + 1; ++i){
			strcpy(dirname_buf + dirname_buf_end, backups[i].dent->d_name);
			rm_dir(dirname_buf);
		}
	}
	closedir(guild_backup_dir);

	snprintf(backup_id_str, sizeof(backup_id_str), "%lu", backups_i ? backups[backups_i - 1].id + 1 : 1);
	free(backups);

	strcat(guild_backup_path, backup_id_str);
	strcat(guild_backup_path, "/");
	make_dir(guild_backup_path, 0755);

	struct discord_channels channels = get_guild_channels(client, guild_id);
	for(int i = 0; i < channels.size; ++i){
		struct discord_channel* chan = channels.array + i;
		if(chan->type == DISCORD_CHANNEL_GUILD_TEXT){
			struct backup_got_messages_data* data = malloc(sizeof(struct backup_got_messages_data));
			data->guild_name = strdup(guild.name);
			data->channel_name = strdup(chan->name);

			char* msg_log_path = malloc(strlen(guild_backup_path) + strlen(chan->name) + strlen(".md") + 2);
			strcpy(msg_log_path, guild_backup_path);
			strcat(msg_log_path, chan->name);
			strcat(msg_log_path, ".md");

			data->msg_log = fopen(msg_log_path, "w");
			if(!data->msg_log){
				fprintf(stderr, "Cannot open \"%s\" for writing\n", msg_log_path);
				return -1;
			}
			free(msg_log_path);

			data->client = client;
			get_all_channel_messages(client, chan->id, _backup_got_messages, data);
		}
	}

	// Send message about backup succeding
	if(has_master_record_guild(guild_id)){
		struct _guild_record gr = get_master_record_guild(guild_id);
		discord_send_message(client, gr.log_channel_id, "Successfuly backed up server messages.");
	}


	free(guild_backup_path);
	discord_channels_cleanup(&channels);
	discord_guild_cleanup(&guild);
	return 0;
}


void msg_scraper_on_interaction(struct discord* client, const struct discord_interaction* e)
{
	if(!strcmp(e->data->name, "backup")){
		set_master_record_guild(e->guild_id, (struct _guild_record){
					.log_channel_id = e->channel_id
				});
		write_master_record();

		discord_interaction_respond(client, e, "Starting backup...");
		if(backup(client, e->guild_id)){
			discord_interaction_response_edit(client, e, "Internal error, check logs");
			sleep(1);
			exit(1);
		}
	}
}
