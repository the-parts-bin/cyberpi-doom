// Network game lobby, in place of Chocolate Doom's text-mode waiting screen
// (net_gui.c): shows who has joined, and lets the host pick the game mode,
// level and options and start the game.
#include <stdio.h>

#include "board.h"
#include "doomgeneric.h"
#include "i_system.h"
#include "i_timer.h"
#include "net_client.h"
#include "net_gui.h"
#include "net_server.h"
#include "netplay.h"
#include "ui.h"

static const uint16_t s_player_colors[4] = {
    UI_RGB(60, 200, 60),   // green
    UI_RGB(130, 130, 240), // indigo
    UI_RGB(180, 125, 60),  // brown
    UI_RGB(230, 50, 50),   // red
};

enum { SET_MODE, SET_MAP, SET_SKILL, SET_MONSTERS, SET_TIME, NUM_SETTINGS };

static const char *const s_labels[NUM_SETTINGS] = {"MODE", "MAP", "SKILL", "MONSTERS", "TIME"};
static const char *const s_modes[] = {"DEATHMATCH", "ALTDEATH", "CO-OP"};
static const char *const s_skills[] = {"BABY", "EASY", "MEDIUM", "HARD", "NIGHTMARE"};
static const int s_times[] = {0, 5, 10, 20};
static const int s_counts[NUM_SETTINGS] = {3, 9, 5, 2, 4};

// Current choice per setting. Shareware Doom has episode 1 only.
static int s_set[NUM_SETTINGS] = {0, 0, 2, 0, 0};

static void value_text(int i, char *buf, size_t len)
{
    int v = s_set[i];
    switch (i) {
    case SET_MODE: snprintf(buf, len, "%s", s_modes[v]); break;
    case SET_MAP: snprintf(buf, len, "E1M%d", v + 1); break;
    case SET_SKILL: snprintf(buf, len, "%s", s_skills[v]); break;
    case SET_MONSTERS: snprintf(buf, len, "%s", v ? "YES" : "NO"); break;
    case SET_TIME: s_times[v] ? snprintf(buf, len, "%d MIN", s_times[v]) : snprintf(buf, len, "NONE"); break;
    }
}

static void draw(int sel)
{
    const net_waitdata_t *w = &net_client_wait_data;
    char buf[40];

    ui_clear(UI_BLACK);
    snprintf(buf, sizeof(buf), "GAME: %s", netplay_current_game);
    ui_text_center(2, buf, UI_YELLOW);

    int y = 15;
    for (int i = 0; i < 4; i++, y += UI_LINE) {
        if (i < w->num_players) {
            ui_fill(6, y + 1, 5, 5, s_player_colors[i]);
            ui_text(16, y, w->player_names[i], i == w->consoleplayer ? UI_WHITE : UI_GREY);
        } else {
            ui_fill(6, y + 3, 5, 1, UI_DARK);
        }
    }

    y += 5;
    if (w->is_controller) {
        for (int i = 0; i < NUM_SETTINGS; i++, y += UI_LINE) {
            if (i == sel)
                ui_fill(0, y - 1, LCD_W, UI_LINE, UI_DARK);
            ui_text(4, y, s_labels[i], UI_GREY);
            value_text(i, buf, sizeof(buf));
            ui_text(LCD_W - 4 - ui_text_width(buf), y, buf, i == sel ? UI_YELLOW : UI_WHITE);
        }
        ui_text_center(LCD_H - UI_LINE, UI_ICON_A " START   MENU QUIT", UI_GREY);
    } else {
        ui_text_center(y + 14, "WAITING FOR THE", UI_WHITE);
        ui_text_center(y + 14 + UI_LINE, "HOST TO START", UI_WHITE);
        ui_text_center(LCD_H - UI_LINE, "MENU LEAVE", UI_GREY);
    }
    ui_present();
}

void NET_WaitForLaunch(void)
{
    int sel = 0;
    int last_draw = -1000;

    ui_buttons();
    for (;;) {
        // net_waiting_for_launch is only updated by NET_CL_Run, and is still
        // false right after connecting: run the client before testing it.
        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
            I_Error("Lost connection to the host");
        if (!net_waiting_for_launch)
            break;

        uint16_t b = ui_buttons();
        if (b & BTN_MENU)
            I_Quit();
        if (net_client_wait_data.is_controller) {
            if (b & BTN_UP)
                sel = (sel + NUM_SETTINGS - 1) % NUM_SETTINGS;
            if (b & BTN_DOWN)
                sel = (sel + 1) % NUM_SETTINGS;
            if (b & BTN_LEFT)
                s_set[sel] = (s_set[sel] + s_counts[sel] - 1) % s_counts[sel];
            if (b & BTN_RIGHT)
                s_set[sel] = (s_set[sel] + 1) % s_counts[sel];
            if (b & BTN_A)
                NET_CL_LaunchGame();
        }

        int now = I_GetTimeMS();
        if (b || now - last_draw > 200) {
            draw(sel);
            last_draw = now;
        }
        I_Sleep(10);
    }
}

const char *DG_NetPlayerName(void)
{
    return netplay_player_name;
}

// Only the host's (controller's) settings are used by the server.
void DG_NetGameSettings(net_gamesettings_t *settings)
{
    settings->deathmatch = s_set[SET_MODE] == 0 ? 1 : s_set[SET_MODE] == 1 ? 2 : 0;
    settings->episode = 1;
    settings->map = s_set[SET_MAP] + 1;
    settings->skill = s_set[SET_SKILL];
    settings->nomonsters = !s_set[SET_MONSTERS];
    settings->timelimit = s_times[s_set[SET_TIME]];
    settings->fast_monsters = 0;
    settings->respawn_monsters = 0;
}
