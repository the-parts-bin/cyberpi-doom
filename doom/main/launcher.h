// Start screen: single player, or host / join a network game.
#pragma once

typedef enum {
    LAUNCH_SINGLE,
    LAUNCH_HOST,
    LAUNCH_JOIN,
} launch_mode_t;

// Runs the start screen until a game is chosen. For LAUNCH_JOIN, *server is
// the host's address. before_network is called once before Wi-Fi starts.
launch_mode_t launcher_run(const char **server, void (*before_network)(void));
