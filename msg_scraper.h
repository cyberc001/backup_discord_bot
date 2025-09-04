#ifndef MSG_SCRAPER_H
#define MSG_SCRAPER_H

#include "discord.h"

void msg_scraper_on_ready(struct discord* client, const struct discord_ready* e, int repeat);
void msg_scraper_on_interaction(struct discord* client, const struct discord_interaction* e);

#endif
