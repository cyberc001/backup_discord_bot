#include "utils.h"
#include <string.h>
#include <stdlib.h>


struct discord_guild get_guild_by_id(struct discord* client, u64snowflake id)
{
        struct discord_guild guild;
        struct discord_ret_guild r_guild = {.sync = &guild};
        discord_get_guild(client, id, &r_guild);
	return guild;
}
struct discord_channels get_guild_channels(struct discord* client, u64snowflake guild_id)
{
        struct discord_channels channels;
        struct discord_ret_channels r_channels = {.sync = &channels};
        discord_get_guild_channels(client, guild_id, &r_channels);
	return channels;
}

struct _get_all_channel_messages_data{
	u64snowflake before;
	u64snowflake channel_id;
	void (*got_msgs)(const struct discord_messages*);
};
static void _get_all_channel_messages_done(struct discord* client, struct discord_response* res, const struct discord_messages* ret)
{
	struct _get_all_channel_messages_data* data = res->data;
	data->got_msgs(ret);
	if(!ret->size){
		free(data);
		return;
	}

	data->before = ret->array[ret->size - 1].id;
	struct discord_get_channel_messages params = {.limit = 100, .before = data->before};
	struct discord_ret_messages r_messages = {.done = _get_all_channel_messages_done, .data = data};
	discord_get_channel_messages(client, data->channel_id, &params, &r_messages);
}
void get_all_channel_messages(struct discord* client, u64snowflake channel_id, void (*got_msgs)(const struct discord_messages*))
{
	struct _get_all_channel_messages_data* data = malloc(sizeof(struct _get_all_channel_messages_data));
	data->channel_id = channel_id;
	data->before = 0;
	data->got_msgs = got_msgs;

	struct discord_get_channel_messages params = {.limit = 100};
	struct discord_ret_messages r_messages = {.done = _get_all_channel_messages_done, .data = data};
	discord_get_channel_messages(client, channel_id, &params, &r_messages);
}
