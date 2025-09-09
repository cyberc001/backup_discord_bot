#include "weather_scraper.h"
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "master_config.h"

static CURL* curl;

struct curl_str {
	size_t ln;
	char* data;
};
size_t curl_read_string(void* ptr, size_t size, size_t nmemb, void* _str)
{
	size_t ptr_ln = size * nmemb;
	struct curl_str* str = _str;

	str->ln += ptr_ln;
	str->data = realloc(str->data, str->ln);
	memcpy(str->data + str->ln - ptr_ln - 1, ptr, ptr_ln);
	return ptr_ln;
}

void weather_scraper_on_ready(struct discord* client, const struct discord_ready* e, int repeat)
{
	if(repeat)
		return;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_read_string);

	struct discord_application_command_option weather_options[] = {
		{
			.type = DISCORD_APPLICATION_OPTION_STRING,
			.name = "city",
			.description = "city to get forecast for",
			.required = 1
		}
	};
        struct discord_create_guild_application_command params = {
                .name = "weather",
                .description = "Display weather forecast",
		.options = &(struct discord_application_command_options){
			.size = sizeof(weather_options) / sizeof(*weather_options),
			.array = weather_options
		}
        };
	for(int i = 0; i < e->guilds->size; ++i)
		discord_create_guild_application_command(client, e->application->id, e->guilds->array[i].id, &params, NULL);
}

enum forecast_weather
{
	CLEAR = 1, FEW_CLOUDS, CLOUDS, BROKEN_CLOUDS,
	SHOWER_RAIN = 9, RAIN, THUNDERSTORM,
	SNOW = 13,
	MIST = 50
};
#define SET_WEATHER(w, s)	{(w) &= ~0xF; (w) |= (s);}
#define SET_RAIN(w, r)		{(w) &= ~0xE0; (w) |= (r);}

struct forecast_item
{
	time_t ts;
	enum forecast_weather weather;
	double temp;
};
struct forecast
{
	struct forecast_item* data;
	size_t ln;

	int err;
};

#define FORECAST_ERROR_PARSE			1
#define FORECAST_ERROR_CITY_NOT_FOUND		2

#define CROP_VAL(_pair) {v_end = json.data + (_pair)->v.pos + (_pair)->v.len; c = *v_end; *v_end = '\0'; }
#define CROP_VAL_OFF(_pair, _off) {v_end = json.data + (_pair)->v.pos + (_pair)->v.len + (_off); c = *v_end; *v_end = '\0'; }
#define UNCROP_VAL() {*v_end = c;}

struct forecast get_weather(const char* url)
{
	struct curl_str json = {1, malloc(1)};
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &json);
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_perform(curl);
	json.data[json.ln - 1] = '\0';

	struct forecast forecast = {.data = NULL, .ln = 0, .err = 0};

        jsmn_parser parser;
        jsmn_init(&parser);
        jsmntok_t* tokens = NULL; unsigned token_cnt = 0;
        int r = jsmn_parse_auto(&parser, json.data, json.ln, &tokens, &token_cnt);
        if(r <= 0){
		free(json.data);
		forecast.err = FORECAST_ERROR_PARSE;
                return forecast;
	}

        jsmnf_loader loader;
        jsmnf_init(&loader);
        jsmnf_pair* pairs = NULL; unsigned pair_cnt = 0;
        r = jsmnf_load_auto(&loader, json.data, tokens, token_cnt, &pairs, &pair_cnt);
        if(r <= 0){
		free(json.data);
		forecast.err = FORECAST_ERROR_PARSE;
                return forecast;
	}

	jsmnf_pair* cod = jsmnf_find(pairs, json.data, "cod", strlen("cod"));
	if(!cod){
		free(json.data);
		forecast.err = FORECAST_ERROR_PARSE;
		return forecast;
	}

	char c; char* v_end;
	CROP_VAL(cod);
	int code = strtol(json.data + cod->v.pos, NULL, 10);
	UNCROP_VAL();
	if(code == 404){
		forecast.err = FORECAST_ERROR_CITY_NOT_FOUND;
		return forecast;
	}

	jsmnf_pair* list = jsmnf_find(pairs, json.data, "list", strlen("list"));
	if(!list){
		free(json.data);
		forecast.err = FORECAST_ERROR_PARSE;
                return forecast;
	}

	for(int i = 0; i < list->size; ++i){
		jsmnf_pair* entry = &list->fields[i];
		forecast.data = realloc(forecast.data, ++forecast.ln * sizeof(struct forecast_item));
		struct forecast_item* f = forecast.data + (forecast.ln - 1);

		jsmnf_pair* dt = jsmnf_find(entry, json.data, "dt", strlen("dt"));
		if(!dt)
			continue;
		CROP_VAL(dt);
		f->ts = strtoull(json.data + dt->v.pos, NULL, 10);
		UNCROP_VAL();

		jsmnf_pair* main = jsmnf_find(entry, json.data, "main", strlen("main"));
		if(!main)
			continue;
		jsmnf_pair* temp = jsmnf_find(main, json.data, "temp", strlen("temp"));
		if(!temp)
			continue;
		CROP_VAL(temp);
		f->temp = strtod(json.data + temp->v.pos, NULL) - 273.15;
		UNCROP_VAL();

		jsmnf_pair* weather = jsmnf_find(entry, json.data, "weather", strlen("weather"));
		if(!weather || !weather->size)
			continue;
		jsmnf_pair* icon = jsmnf_find(weather->fields, json.data, "icon", strlen("icon"));
		if(!icon)
			continue;
		CROP_VAL_OFF(icon, -1);
		f->weather = strtol(json.data + icon->v.pos, NULL, 10);
		UNCROP_VAL();

	}

	free(json.data);
	return forecast;
}

void weather_scraper_on_interaction(struct discord* client, const struct discord_interaction* e)
{
        if(!strcmp(e->data->name, "weather")){
		const char* city = e->data->options->array[0].value;
		char req_city[128];
		size_t city_ln = strlen(city);
		size_t i = 0, j = 0; for(; j < city_ln; ++j)
			if(city[j] == ' '){
				req_city[i++] = '%';
				req_city[i++] = '2';
				req_city[i++] = '0';
			}
			else
				req_city[i++] = city[j];
		req_city[i] = '\0';

		char req[1024];
		snprintf(req, sizeof(req), "api.openweathermap.org/data/2.5/forecast?q=%s&appid=%s", req_city, master_config.owm_appid);
		struct forecast forecast = get_weather(req);

		switch(forecast.err){
			case FORECAST_ERROR_PARSE: discord_interaction_respond(client, e, "Как это прочитать? %s", get_inchar_append(INCHAR_TYPE_SAD)); return;
			case FORECAST_ERROR_CITY_NOT_FOUND: discord_interaction_respond(client, e, "Я не знаю такого города, %s", get_inchar_append(INCHAR_TYPE_SAD)); return;
		}

		// nipah or meep depending on smount of sunny entries
		size_t sunny = 0;
		for(size_t i = 0; i < forecast.ln; ++i)
			switch(forecast.data[i].weather){
				case CLEAR: case FEW_CLOUDS:
					++sunny;
					break;
			}

		int inchar_types = sunny * 2 >= forecast.ln ? INCHAR_TYPE_SAD : INCHAR_TYPE_CHEERFUL;

		char msg[2001];
		msg[0] = '\0';
		int msg_pos = 0;

		int prev_tm_day = -1, prev_tm_mon = -1;
		size_t prev_tm_i = 0;
		msg_pos += snprintf(msg + msg_pos, sizeof(msg) - 1 - msg_pos, "# Погода в городе %s, %s\n", city, get_inchar_append(inchar_types));
		for(size_t i = 0; i < forecast.ln; ++i){
			struct tm lt; localtime_r(&forecast.data[i].ts, &lt);
			if(lt.tm_mday != prev_tm_day || i == forecast.ln - 1){
				if(prev_tm_day > -1){
					struct tm lt2; localtime_r(&forecast.data[i].ts, &lt2);
					msg_pos += snprintf(msg + msg_pos, sizeof(msg) - 1 - msg_pos, "\n## `%02d/%02d`\n ", prev_tm_day, prev_tm_mon + 1);
					for(size_t j = prev_tm_i; i == forecast.ln - 1 ? (j <= i) : (j < i); ++j){
						localtime_r(&forecast.data[j].ts, &lt2);
						msg_pos += snprintf(msg + msg_pos, sizeof(msg) - 1 - msg_pos, "      `%02d:00` ", lt2.tm_hour);
					}
					strcat(msg, "\n"); ++msg_pos;
					for(size_t j = prev_tm_i; i == forecast.ln - 1 ? (j <= i) : (j < i); ++j){
						const char* weather_emoji = ":question:";
						switch(forecast.data[j].weather){
							case CLEAR: weather_emoji = ":sunny:"; break;
							case FEW_CLOUDS: weather_emoji = ":white_sun_cloud:"; break;
							case CLOUDS: case BROKEN_CLOUDS: weather_emoji = ":cloud:"; break;
							case SHOWER_RAIN: weather_emoji = ":cloud_rain:"; break;
							case RAIN: weather_emoji = ":white_sun_rain_cloud:"; break;
							case THUNDERSTORM: weather_emoji = ":thunder_cloud_rain:"; break;
							case SNOW: weather_emoji = ":cloud_snow:"; break;
							case MIST: weather_emoji = ":fog:"; break;
						}
						localtime_r(&forecast.data[j].ts, &lt2);
						msg_pos += snprintf(msg + msg_pos, sizeof(msg) - 1 - msg_pos, "%s `%+5.1f`", weather_emoji, forecast.data[j].temp);
					}
				}

				prev_tm_day = lt.tm_mday;
				prev_tm_mon = lt.tm_mon;
				prev_tm_i = i;
			}
		}
		discord_interaction_respond(client, e, msg);

		free(forecast.data);
	}
}
