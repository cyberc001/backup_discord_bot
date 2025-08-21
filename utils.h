#ifndef UTILS_H
#define UTILS_H

#include <sys/stat.h>
#include "discord.h"

void discord_interaction_respond(struct discord* client, const struct discord_interaction* interaction, const char* format, ...);
void discord_interaction_response_edit(struct discord* client, const struct discord_interaction* interaction, const char* format, ...);

struct discord_guild get_guild_by_id(struct discord* client, u64snowflake id);
struct discord_channels get_guild_channels(struct discord* client, u64snowflake guild_id);

void get_all_channel_messages(struct discord* client, u64snowflake channel_id, void (*got_msgs)(const struct discord_messages*, void*), void* user_data);

// recursive mkdir. Does not return an error if a directory already exists.
int make_dir(const char* path, mode_t mode); 

#endif
