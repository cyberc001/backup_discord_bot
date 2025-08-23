#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "discord.h"

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
	int err = init_master_record("master_record.bin");
	switch(err){
		case ERROR_CANNOT_READ_MASTER_RECORD:
			fprintf(stderr, "Invalid master record; cannot read\n");
			return 1;
		case ERROR_CANNOT_WRITE_MASTER_RECORD:
			fprintf(stderr, "Cannot open \"master_record.bin\" for writing\n");
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
