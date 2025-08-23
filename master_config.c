#include "master_config.h"

#include <jsmn.h>
#include <jsmn-find.h>

struct _master_config master_config;

#define READ_UNSIGNED(var, root, key, def_val){\
	jsmnf_pair* f = jsmnf_find((root), json, (key), strlen(key));\
	if(!f)\
		(var) = (def_val);\
	else {\
		char* endptr = NULL;\
		char* str = malloc(f->v.len + 1); memcpy(str, json + f->v.pos, f->v.len + 1); str[f->v.len] = '\0';\
		(var) = strtoul(str, &endptr, 10);\
		if(*endptr){\
			free(tokens); free(pairs); free(json);\
			fprintf(stderr, "Error: expected unsigned number, got %s\n", str);\
			free(str);\
			return ERROR_CANNOT_PARSE_MASTER_CONFIG;\
		}\
		free(str);\
	}\
}

int init_master_config(const char* fname)
{
	FILE* fd = fopen(fname, "r");
	if(!fd)
		return ERROR_CANNOT_READ_MASTER_CONFIG;

	fseek(fd, 0, SEEK_END);
	long fsize = ftell(fd);
	rewind(fd);

	char* json = malloc(fsize + 1);
	fread(json, 1, fsize, fd);
	json[fsize] = '\0';

	jsmn_parser parser;
	jsmn_init(&parser);
	jsmntok_t* tokens = NULL; unsigned token_cnt = 0;
	int r = jsmn_parse_auto(&parser, json, strlen(json), &tokens, &token_cnt);
	if(r <= 0){
		free(json);
		return ERROR_CANNOT_PARSE_MASTER_CONFIG;
	}

	jsmnf_loader loader;
	jsmnf_init(&loader);
	jsmnf_pair* pairs = NULL; unsigned pair_cnt = 0;
	r = jsmnf_load_auto(&loader, json, tokens, token_cnt, &pairs, &pair_cnt);
	if(r <= 0){
		free(json);
		free(tokens);
		return ERROR_CANNOT_PARSE_MASTER_CONFIG;
	}
	READ_UNSIGNED(master_config.max_backups, pairs, "max_backups", 3);
	READ_UNSIGNED(master_config.backup_interval, pairs, "backup_interval", 10);
	
	free(tokens);
	free(pairs);
	free(json);
	return 0;
}
