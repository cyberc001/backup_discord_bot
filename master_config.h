#ifndef MASTER_CONFIG_H
#define MASTER_CONFIG_H

#include <discord.h>
#include <time.h>

extern struct _master_config {
	size_t max_backups;		// per server
	time_t backup_interval;

	char* owm_appid; // openweathermap token
} master_config;

#define ERROR_CANNOT_READ_MASTER_CONFIG		-1
#define ERROR_CANNOT_PARSE_MASTER_CONFIG	-2
int init_master_config(struct discord* client, const char* fname);

#endif
