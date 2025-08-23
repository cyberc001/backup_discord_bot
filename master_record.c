#include "master_record.h"
#include <stdlib.h>
#include <stdio.h>

#include <signal.h>

#include <chash.h>
#include <string.h>
#define GUILD_T_HEAP				1
#define GUILD_T_BUCKET				struct guild_record
#define GUILD_T_FREE_KEY(key)
#define GUILD_T_HASH(key, hash)			key
#define GUILD_T_FREE_VALUE(value)		NULL
#define GUILD_T_COMPARE(cmp_a, cmp_b)		cmp_a == cmp_b
#define GUILD_T_INIT(bucket, _key, _value)	chash_default_init(bucket, _key, _value)

struct _master_record master_record;
static FILE* master_fd;

#define MR_READ_FIELD(field){\
	if(!fread(&((master_record).field), sizeof((master_record).field), 1, fd))\
		return ERROR_CANNOT_READ_MASTER_RECORD;\
}
#define MR_READ(var){\
	if(!fread(&(var), sizeof(var), 1, fd))\
		return ERROR_CANNOT_READ_MASTER_RECORD;\
}

#define MR_WRITE_FIELD(field){\
	fwrite(&((master_record).field), sizeof((master_record).field), 1, master_fd);\
}
#define MR_WRITE(var){\
	fwrite(&(var), sizeof(var), 1, master_fd);\
}


void exit_handler(int signal)
{
	fclose(master_fd);
	exit(0);
}

void write_master_record()
{
	rewind(master_fd);
	MR_WRITE_FIELD(last_backup_time);

	size_t server_count = master_record.guild_records->length;
	MR_WRITE(server_count);
	for(int i = 0; i < master_record.guild_records->capacity; ++i){
		if(master_record.guild_records->buckets[i].state != CHASH_FILLED)
			continue;
		MR_WRITE(master_record.guild_records->buckets[i].key);
		MR_WRITE(master_record.guild_records->buckets[i].value);
	}
}

int init_master_record(const char* fname)
{
	master_record.guild_records = chash_init(master_record.guild_records, GUILD_T);

        FILE* fd = fopen(fname, "rb");
	if(fd){
		MR_READ_FIELD(last_backup_time);

		size_t server_count; MR_READ(server_count);
		for(size_t i = 0; i < server_count; ++i){
			u64snowflake key; MR_READ(key);
			struct _guild_record value; MR_READ(value);
			chash_assign(master_record.guild_records, key, value, GUILD_T);
		}

		fclose(fd);
	}
	master_fd = fopen(fname, "wb+");
	if(!master_fd)
		return ERROR_CANNOT_WRITE_MASTER_RECORD;
	if(!fd)
		write_master_record();

	signal(SIGINT, exit_handler);
	//signal(SIGSEGV, exit_handler);
        return 0;
}

void set_master_record_guild(u64snowflake key, struct _guild_record value)
{
	chash_assign(master_record.guild_records, key, value, GUILD_T);
}
int has_master_record_guild(u64snowflake key)
{
	int ret = chash_contains(master_record.guild_records, key, ret, GUILD_T);
	return ret;
}
struct _guild_record get_master_record_guild(u64snowflake key)
{
	struct _guild_record ret = chash_lookup(master_record.guild_records, key, ret, GUILD_T);
	return ret;
}
