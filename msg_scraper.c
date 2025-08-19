#include "msg_scraper.h"
#include <string.h>

void msg_scraper_on_ready(struct discord *client, const struct discord_ready *e)
{
	struct discord_create_guild_application_command params = {
		.name = "backup",
		.description = "Initiate backup manually"
	};
	for(int i = 0; i < e->guilds->size; ++i)
		discord_create_guild_application_command(client, e->application->id, e->guilds->array[i].id, &params, NULL);
}

void msg_scraper_on_interaction(struct discord *client, const struct discord_interaction *e)
{
	if(!strcmp(e->data->name, "backup")){
		struct discord_interaction_response params = {
			.type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
			.data = &(struct discord_interaction_callback_data){
				.content = "Starting backup..."
			}
		};
		discord_create_interaction_response(client, e->id, e->token, &params, NULL);
	}
}
