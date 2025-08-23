#include "utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

void discord_send_message(struct discord* client, u64snowflake channel_id, const char* format, ...)
{
	va_list fargs;
	va_start(fargs, format);
	char msg[2048];
	vsnprintf(msg, sizeof(msg), format, fargs);
	va_end(fargs);

	struct discord_create_message params = {.content = msg};
	discord_create_message(client, channel_id, &params, NULL);
}

void discord_interaction_respond(struct discord* client, const struct discord_interaction* interaction, const char* format, ...)
{
	va_list fargs;
	va_start(fargs, format);
	char msg[2048];
	vsnprintf(msg, sizeof(msg), format, fargs);
	va_end(fargs);

	struct discord_interaction_response params = {
		.type = DISCORD_INTERACTION_CHANNEL_MESSAGE_WITH_SOURCE,
		.data = &(struct discord_interaction_callback_data){
			.content = msg
		}
	};
	discord_create_interaction_response(client, interaction->id, interaction->token, &params, NULL);
}
void discord_interaction_response_edit(struct discord* client, const struct discord_interaction* interaction, const char* format, ...)
{
	va_list fargs;
	va_start(fargs, format);
	char msg[2048];
	vsnprintf(msg, sizeof(msg), format, fargs);
	va_end(fargs);

	struct discord_edit_original_interaction_response params = {
		.content = msg
	};
	discord_edit_original_interaction_response(client, interaction->application_id, interaction->token, &params, NULL);
}

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
	void (*got_msgs)(const struct discord_messages*, void*);
	void* user_data;
};
static void _get_all_channel_messages_done(struct discord* client, struct discord_response* res, const struct discord_messages* ret)
{
	struct _get_all_channel_messages_data* data = res->data;
	data->got_msgs(ret, data->user_data);
	if(!ret->size){
		free(data);
		return;
	}

	data->before = ret->array[ret->size - 1].id;
	struct discord_get_channel_messages params = {.limit = 100, .before = data->before};
	struct discord_ret_messages r_messages = {.done = _get_all_channel_messages_done, .data = data};
	discord_get_channel_messages(client, data->channel_id, &params, &r_messages);
}
void get_all_channel_messages(struct discord* client, u64snowflake channel_id, void (*got_msgs)(const struct discord_messages*, void*), void* user_data)
{
	struct _get_all_channel_messages_data* data = malloc(sizeof(struct _get_all_channel_messages_data));
	data->channel_id = channel_id;
	data->before = 0;
	data->got_msgs = got_msgs;
	data->user_data = user_data;

	struct discord_get_channel_messages params = {.limit = 100};
	struct discord_ret_messages r_messages = {.done = _get_all_channel_messages_done, .data = data};
	discord_get_channel_messages(client, channel_id, &params, &r_messages);
}

int make_dir(const char* _path, mode_t mode)
{
	char* path_alloc = strdup(_path);
	char *path = path_alloc;
	int err;
	for(; *path; ++path){
		char c = *path;
		if(c == '/' || c == '\\'){
			*path = '\0';
			if((err = mkdir(path_alloc, mode)) && errno != EEXIST){
				free(path_alloc);
				return err;
			}
			*path = c;
		}
	}
	if((err = mkdir(path_alloc, mode)) && errno != EEXIST){
		free(path_alloc);
		return err;
	}
	free(path_alloc);
	return 0;
}

// at any depth path_buf is only modified past path_buf_end, allowing to keep the same path in one buffer
static int _rm_dir(char* path_buf, size_t path_buf_end)
{
	struct dirent* dent;
	DIR* srcdir = opendir(path_buf);
	if(!srcdir){
		closedir(srcdir);
		return ERROR_CANNOT_OPEN_DIR;
	}

	while((dent = readdir(srcdir))){
		if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
				(dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
			continue;

		struct stat st;
		if(fstatat(dirfd(srcdir), dent->d_name, &st, 0) < 0){
			closedir(srcdir);
			return ERROR_CANNOT_STAT_FILE;
		}

		if(S_ISDIR(st.st_mode)){
			size_t path_buf_end_inc = strlen(dent->d_name);
			memcpy(path_buf + path_buf_end, dent->d_name, path_buf_end_inc);
			path_buf[path_buf_end + path_buf_end_inc++] = '/';
			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			_rm_dir(path_buf, path_buf_end + path_buf_end_inc);

			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			rmdir(path_buf);
		} else if(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)){
			size_t path_buf_end_inc = strlen(dent->d_name);
			memcpy(path_buf + path_buf_end, dent->d_name, path_buf_end_inc);
			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			unlink(path_buf);
		}
	}
	closedir(srcdir);
	return 0;
}
int rm_dir(const char* path)
{
	struct dirent* dent;
	DIR* srcdir = opendir(path);
	if(!srcdir)
		return ERROR_CANNOT_OPEN_DIR;

	char path_buf[4096];
	strcpy(path_buf, path);
	size_t path_buf_end = strlen(path_buf);
	char last_path_char = path[path_buf_end - 1];
	if(last_path_char != '/' && last_path_char != '\\'){
		path_buf[path_buf_end++] = '/';
		path_buf[path_buf_end] = '\0';
	}

	while((dent = readdir(srcdir))){
		if((dent->d_name[0] == '.' && dent->d_name[1] == '\0') ||
				(dent->d_name[0] == '.' && dent->d_name[1] == '.' && dent->d_name[2] == '\0'))
			continue;

		struct stat st;
		if(fstatat(dirfd(srcdir), dent->d_name, &st, 0) < 0){
			closedir(srcdir);
			return ERROR_CANNOT_STAT_FILE;
		}

		if(S_ISDIR(st.st_mode)){
			size_t path_buf_end_inc = strlen(dent->d_name);
			memcpy(path_buf + path_buf_end, dent->d_name, path_buf_end_inc);
			path_buf[path_buf_end + path_buf_end_inc++] = '/';
			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			int err;
			if((err = _rm_dir(path_buf, path_buf_end + path_buf_end_inc))){
				closedir(srcdir);
				return err;
			}

			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			rmdir(path_buf);
		} else if(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)){
			size_t path_buf_end_inc = strlen(dent->d_name);
			memcpy(path_buf + path_buf_end, dent->d_name, path_buf_end_inc);
			path_buf[path_buf_end + path_buf_end_inc] = '\0';
			unlink(path_buf);
		}
	}
	closedir(srcdir);

	rmdir(path);
	return 0;
}
