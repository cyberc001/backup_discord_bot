INCLUDE := -I. -I/usr/local/include/concord
FLAGS := -fsanitize=address -Wall -Wextra -Wno-unused-parameter -g
LIBS := -ldiscord -lcurl

CC := gcc $(INCLUDE) $(FLAGS)
CCO := $(CC) -c

all: backup_discord_bot
clean:
	-rm backup_discord_bot
	-rm *.o

backup_discord_bot: main.o msg_scraper.o utils.o
	$(CC) $^ -o $@ $(LIBS)
main.o: main.c
	$(CCO) $< -o $@
msg_scraper.o: msg_scraper.c msg_scraper.h
	$(CCO) $< -o $@
utils.o: utils.c utils.h
	$(CCO) $< -o $@
