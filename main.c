#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "discord.h"
#include "log.h"

#include "master_config.h"
#include "master_record.h"
#include "msg_scraper.h"

void on_ready(struct discord* client, const struct discord_ready* e)
{
	msg_scraper_on_ready(client, e);
}
void on_interaction(struct discord* client, const struct discord_interaction* e)
{
	if(e->type != DISCORD_INTERACTION_APPLICATION_COMMAND)
		return;
	msg_scraper_on_interaction(client, e);
}

int main(int argc, const char** argv)
{
	const char* master_record_fname = "master_record.bin";
	int err = init_master_record(master_record_fname);
	switch(err){
		case ERROR_CANNOT_READ_MASTER_RECORD:
			log_fatal("Invalid master record; cannot read \"%s\"", master_record_fname);
			return 1;
		case ERROR_CANNOT_WRITE_MASTER_RECORD:
			log_fatal("Cannot open \"%s\" for writing", master_record_fname);
			return 1;
	}

	const char* master_config_fname = "config.json";
	err = init_master_config(master_config_fname);
	switch(err){
		case ERROR_CANNOT_READ_MASTER_CONFIG:
			log_fatal("Cannot open master config \"%s\" for reading", master_config_fname);
			return 1;
		case ERROR_CANNOT_PARSE_MASTER_CONFIG:
			log_fatal("Cannot parse master config \"%s\"", master_config_fname);
			return 1;
	}

	ccord_global_init();
	struct discord* client = discord_config_init("config.json");
	assert(client && "Couldn't initialize client");

	discord_add_intents(client, DISCORD_GATEWAY_MESSAGE_CONTENT);
	discord_set_on_ready(client, on_ready);
	discord_set_on_interaction_create(client, on_interaction);

	discord_run(client);

	discord_cleanup(client);
	ccord_global_cleanup();
}
