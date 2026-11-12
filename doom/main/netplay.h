// Wi-Fi for network games: the host runs an access point named after the
// game, the other players join it as stations.
#pragma once

#include <stdbool.h>

#define NETPLAY_NAME_LEN 10 // player and game names, without the terminator
#define NETPLAY_MAX_GAMES 8

typedef struct {
    char name[NETPLAY_NAME_LEN + 1];
    int rssi;
} netplay_game_t;

// Player and game names, loaded from /data/netplay.txt (defaults derived
// from the MAC address).
extern char netplay_player_name[NETPLAY_NAME_LEN + 1];
extern char netplay_game_name[NETPLAY_NAME_LEN + 1];
void netplay_load_names(void);
void netplay_save_names(void);

// The game being hosted or joined.
extern char netplay_current_game[NETPLAY_NAME_LEN + 1];

// Starts the access point for a game named netplay_game_name.
bool netplay_host(void);

// Scans for games; returns how many were found, strongest first.
int netplay_scan(netplay_game_t *games, int max);

// Joins a game and waits for an address. On success, *server is the host's
// address as a string for -connect.
bool netplay_join(const char *game_name, const char **server);

// A message to show on the next boot, kept across a restart (e.g. why the
// last game ended). netplay_take_message returns NULL if there is none.
void netplay_set_message(const char *msg);
const char *netplay_take_message(void);
