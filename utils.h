#ifndef UTILS_H
#define UTILS_H

#include <sys/stat.h>
#include <discord.h>
#include <discord-internal.h>

#define dlog_trace(client, msg, ...) logconf_trace(&(client)->conf, msg, ##__VA_ARGS__)
#define dlog_info(client, msg, ...) logconf_info(&(client)->conf, msg, ##__VA_ARGS__)
#define dlog_warn(client, msg, ...) logconf_warn(&(client)->conf, msg, ##__VA_ARGS__)
#define dlog_error(client, msg, ...) logconf_error(&(client)->conf, msg, ##__VA_ARGS__)
#define dlog_fatal(client, msg, ...) logconf_fatal(&(client)->conf, msg, ##__VA_ARGS__)

void discord_send_message(struct discord* client, u64snowflake channel_id, const char* format, ...);

void discord_interaction_respond(struct discord* client, const struct discord_interaction* interaction, const char* format, ...);
void discord_interaction_response_edit(struct discord* client, const struct discord_interaction* interaction, const char* format, ...);


// In-character responses
#define INCHAR_TYPE_CHEERFUL		1
#define INCHAR_TYPE_SAD			2
#define INCHAR_TYPE_ANY			0xFFFF
const char* get_inchar_append(int types);


struct discord_guild get_guild_by_id(struct discord* client, u64snowflake id);
struct discord_channels get_guild_channels(struct discord* client, u64snowflake guild_id);

void get_all_channel_messages(struct discord* client, u64snowflake channel_id, void (*got_msgs)(const struct discord_messages*, void*), void* user_data);

// recursive mkdir. Does not return an error if a directory already exists.
int make_dir(const char* path, mode_t mode); 
// recursively delete all files in the directory and the directory itself
#define ERROR_CANNOT_OPEN_DIR		-1
#define ERROR_CANNOT_STAT_FILE		-2
int rm_dir(const char* path);

#endif
