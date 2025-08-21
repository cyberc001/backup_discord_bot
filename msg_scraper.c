#include "msg_scraper.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
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
		for(int i = 0; i < msgs->size; ++i){
			fprintf(data->msg_log, "[%s]: %s\n", msgs->array[i].author->username, msgs->array[i].content);
		}
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
		struct discord_channels channels = get_guild_channels(client, e->guild_id);
		for(int i = 0; i < channels.size; ++i){
			struct discord_channel* chan = channels.array + i;
			if(chan->type == DISCORD_CHANNEL_GUILD_TEXT){
				struct backup_got_messages_data* data = malloc(sizeof(struct backup_got_messages_data));
				data->guild_name = strdup(guild.name);
				data->channel_name = strdup(chan->name);

				char* msg_log_path = malloc(strlen("backup") + strlen(guild.name) + strlen(chan->name) + strlen(".md") + 3);
				strcpy(msg_log_path, "backup/");
				strcat(msg_log_path, guild.name);
				strcat(msg_log_path, "/");
				make_dir(msg_log_path, 0755);
				strcat(msg_log_path, chan->name);
				strcat(msg_log_path, ".md");

				data->msg_log = fopen(msg_log_path, "w");
				if(!data->msg_log){
					discord_interaction_response_edit(client, e, "Cannot open \"%s\" for writing", msg_log_path);
					sleep(1); // I could have made it wait for a callback, but thats complicated for a rare error message
					exit(1);
				}

				data->client = client;
				data->interaction = *e; // interaction is going to be freed - save id and token
				get_all_channel_messages(client, chan->id, _backup_got_messages, data);
			}
		}

		discord_channels_cleanup(&channels);
		discord_guild_cleanup(&guild);
	}
}
