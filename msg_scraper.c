#include "msg_scraper.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include "utils.h"

void msg_scraper_on_ready(struct discord *client, const struct discord_ready *e)
{
	struct discord_create_guild_application_command params = {
		.name = "backup",
		.description = "Initiate backup manually"
	};
	for(int i = 0; i < e->guilds->size; ++i)
		discord_create_guild_application_command(client, e->application->id, e->guilds->array[i].id, &params, NULL);
}

struct backup_got_messages_data
{
	char* guild_name;
	char* channel_name;
	FILE* msg_log;

	struct discord* client;
	struct discord_interaction interaction;
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
void msg_scraper_on_interaction(struct discord *client, const struct discord_interaction *e)
{
	if(!strcmp(e->data->name, "backup")){
		discord_interaction_respond(client, e, "Starting backup...");

		struct discord_guild guild = get_guild_by_id(client, e->guild_id);
		char backup_id_str[10];
		char* guild_backup_path = malloc(strlen("backup") + strlen(guild.name) + sizeof(backup_id_str) + 4);
		strcpy(guild_backup_path, "backup/");
		strcat(guild_backup_path, guild.name);
		make_dir(guild_backup_path, 0755);
		strcat(guild_backup_path, "/"); // goes after make_dir() to have it iterate 1 less time

		// scan directory for all subdirectories and sort them. Smallest ID = oldest backup
		unsigned long min_id = ULONG_MAX, max_id = 0;
		struct dirent* dent;
		DIR* guild_backup_dir = opendir(guild_backup_path);
		while((dent = readdir(guild_backup_dir))){
			struct stat st;
			if(!strcmp(dent->d_name, ".") || !strcmp(dent->d_name, ".."))
				continue;

			fstatat(dirfd(guild_backup_dir), dent->d_name, &st, 0);
			if(S_ISDIR(st.st_mode)){
				fprintf(stderr, "DIR %s\n", dent->d_name);
				char* endptr = NULL;
				unsigned long id = strtoul(dent->d_name, &endptr, 10);
				if(*endptr) // invalid ID (not a number)
					continue;
				if(id < min_id)
					min_id = id;
				if(id > max_id)
					max_id = id;
			}
		}
		snprintf(backup_id_str, sizeof(backup_id_str), "%lu", ++max_id);
		strcat(guild_backup_path, backup_id_str);
		strcat(guild_backup_path, "/");
		make_dir(guild_backup_path, 0755);

		struct discord_channels channels = get_guild_channels(client, e->guild_id);
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
					discord_interaction_response_edit(client, e, "Cannot open \"%s\" for writing", msg_log_path);
					sleep(1); // I could have made it wait for a callback, but thats complicated for a rare error message
					exit(1);
				}
				free(msg_log_path);

				data->client = client;
				data->interaction = *e; // interaction is going to be freed - save id and token
				get_all_channel_messages(client, chan->id, _backup_got_messages, data);
			}
		}

		free(guild_backup_path);
		discord_channels_cleanup(&channels);
		discord_guild_cleanup(&guild);
	}
}
