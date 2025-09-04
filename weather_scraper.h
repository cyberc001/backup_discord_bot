#ifndef WEATHER_SCRAPER_H
#define WEATHER_SCRAPER_H

#include "discord.h"

void weather_scraper_on_ready(struct discord* client, const struct discord_ready* e, int repeat);
void weather_scraper_on_interaction(struct discord* client, const struct discord_interaction* e);

#endif
