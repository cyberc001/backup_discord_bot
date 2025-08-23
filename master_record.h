#ifndef MASTER_RECORD_H
#define MASTER_RECORD_H

#include <discord.h>
#include <time.h>

struct _guild_record {
	u64snowflake log_channel_id;
};
struct guild_record {
	u64snowflake key;
	struct _guild_record value;
	int state;
};
struct _guild_records {
	int length;
	int capacity;
	struct guild_record* buckets;
};
struct _master_record {
	time_t last_backup_time;
	struct _guild_records* guild_records;
};
extern struct _master_record master_record;

#define ERROR_CANNOT_READ_MASTER_RECORD		-1
#define ERROR_CANNOT_WRITE_MASTER_RECORD	-2
int init_master_record(const char* fname);

void set_master_record_guild(u64snowflake key, struct _guild_record value);
int has_master_record_guild(u64snowflake key);
struct _guild_record get_master_record_guild(u64snowflake key);

void write_master_record();

#endif
