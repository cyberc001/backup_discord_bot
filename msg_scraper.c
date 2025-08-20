#include "msg_scraper.h"
#include <string.h>
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

void _backup_got_messages(const struct discord_messages* msgs)
{
	for(int i = 0; i < msgs->size; ++i){
		printf("%s\n", msgs->array[i].content);
	}
}
void msg_scraper_on_interaction(struct discord *client, const struct discord_interaction *e)
{
	if(!strcmp(e->data->name, "backup")){
		struct discord_channels channels = get_guild_channels(client, e->guild_id);
		for(int i = 0; i < channels.size; ++i){
			struct discord_channel* chan = channels.array + i;
			if(chan->type == DISCORD_CHANNEL_GUILD_TEXT && chan->id == 1396667972038430762)
				get_all_channel_messages(client, chan->id, _backup_got_messages);
		}

		discord_channels_cleanup(&channels);
	}
}
