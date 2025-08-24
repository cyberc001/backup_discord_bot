#include "msg_scraper.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include <pthread.h>
#include <stdatomic.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "master_config.h"
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

	time_t now = time(NULL);
	discord_timer_interval(client, backup_timer, backup_timer, NULL,
					(now > master_record.last_backup_time + master_config.backup_interval ? 0 :
					now > master_record.last_backup_time ? master_config.backup_interval - (now - master_record.last_backup_time) :
					master_config.backup_interval) * 1000,
					master_config.backup_interval * 1000, -1);
}

struct backup_got_messages_data
{
	u64snowflake guild_id;
	char files_path_buffer[4096];
	size_t files_path_end; // where the path ends; pre-calculated before getting messages

	// resources per channel that should be freed
	char* guild_name;
	char* channel_name;
	FILE* msg_log;

	// shared resources that should be freed when last channel finishes
	atomic_int* channels_left;
	CURL* curl;

	struct discord* client;
};
static void _backup_got_messages(const struct discord_messages* msgs, void* _data)
{
	struct backup_got_messages_data* data = _data;

	if(msgs->size){
		for(int i = 0; i < msgs->size; ++i){
			fprintf(data->msg_log, "\\[%s\\]: %s\n\n", msgs->array[i].author->username, msgs->array[i].content);

			struct discord_attachments* attachments = msgs->array[i].attachments;
			for(int j = 0; j < attachments->size; ++j){
				struct discord_attachment* att = attachments->array + j;
				fprintf(stderr, "attachment %s %d %s\n", att->filename, att->height, att->content_type);

				snprintf(data->files_path_buffer + data->files_path_end, sizeof(data->files_path_buffer) - data->files_path_end,
						"%lu_", msgs->array[i].id);
				strcat(data->files_path_buffer + data->files_path_end, att->filename); // +end just speeds up iterating a bit
				FILE* fd = fopen(data->files_path_buffer, "wb");
				curl_easy_setopt(data->curl, CURLOPT_WRITEDATA, fd);
				curl_easy_setopt(data->curl, CURLOPT_URL, att->url);
				curl_easy_perform(data->curl);
				fclose(fd);

				if(strlen(att->content_type) >= strlen("image") && !memcmp(att->content_type, "image", strlen("image")))
					fprintf(data->msg_log, "!");
				fprintf(data->msg_log, "[%s](%s)\n", att->filename, data->files_path_buffer + data->files_path_end - strlen("files/"));
			}
		}
	} else {
		// Send message about backup succeding
		if(!--(*data->channels_left) && has_master_record_guild(data->guild_id)){
			free(data->channels_left);
			struct _guild_record gr = get_master_record_guild(data->guild_id);
			discord_send_message(data->client, gr.log_channel_id, "Successfuly backed up server messages.");
			curl_easy_cleanup(data->curl);
		}

		fclose(data->msg_log);
		free(data->guild_name);
		free(data->channel_name);
		free(data);
	}
}
static size_t _curl_write_data(void* ptr, size_t size, size_t nmemb, void* stream)
{
	return fwrite(ptr, size, nmemb, (FILE*)stream);
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
	if(backups_i >= master_config.max_backups){
		for(size_t i = 0; i < backups_i - master_config.max_backups + 1; ++i){
			strcpy(dirname_buf + dirname_buf_end, backups[i].dent->d_name);
			rm_dir(dirname_buf);
		}
	}
	closedir(guild_backup_dir);

	snprintf(backup_id_str, sizeof(backup_id_str), "%lu", backups_i ? backups[backups_i - 1].id + 1 : 1);
	free(backups);

	strcat(guild_backup_path, backup_id_str);
	make_dir(guild_backup_path, 0755);
	strcat(guild_backup_path, "/");

	char* files_path = malloc(strlen(guild_backup_path) + strlen("files") + 2);
	strcpy(files_path, guild_backup_path);
	strcat(files_path, "files/");
	make_dir(files_path, 0755);
	size_t files_path_ln = strlen(files_path);

	struct discord_channels channels = get_guild_channels(client, guild_id);
	atomic_int* channels_left = malloc(sizeof(atomic_int));
	*channels_left = 0;
	for(int i = 0; i < channels.size; ++i)
		*channels_left += (channels.array[i].type == DISCORD_CHANNEL_GUILD_TEXT);

	CURL* curl;
	if(!*channels_left)
		free(channels_left); // no messages will be fetched, so avoid a memory leak
	else{
		curl = curl_easy_init();
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _curl_write_data);
	}

	for(int i = 0; i < channels.size; ++i){
		struct discord_channel* chan = channels.array + i;
		if(chan->type == DISCORD_CHANNEL_GUILD_TEXT){
			struct backup_got_messages_data* data = malloc(sizeof(struct backup_got_messages_data));
			data->guild_id = guild.id;
			data->guild_name = strdup(guild.name);
			data->channel_name = strdup(chan->name);
			data->channels_left = channels_left;

			char* msg_log_path = malloc(strlen(guild_backup_path) + strlen(chan->name) + strlen(".md") + 2);
			strcpy(msg_log_path, guild_backup_path);
			strcat(msg_log_path, chan->name);
			strcat(msg_log_path, ".md");

			strcpy(data->files_path_buffer, files_path);
			data->files_path_end = files_path_ln;
			data->msg_log = fopen(msg_log_path, "w");
			if(!data->msg_log){
				fprintf(stderr, "Cannot open \"%s\" for writing\n", msg_log_path);
				return -1;
			}
			free(msg_log_path);

			data->client = client;
			data->curl = curl;
			get_all_channel_messages(client, chan->id, _backup_got_messages, data);
		}
	}

	free(guild_backup_path);
	free(files_path);
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
