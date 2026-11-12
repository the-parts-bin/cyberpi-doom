#include "launcher.h"

#include <stdio.h>
#include <string.h>

#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "netplay.h"
#include "ui.h"

#define MENU_Y 54
#define FRAME_MS 20

static void wait_frame(void)
{
    vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
}

static void draw_footer(const char *hint)
{
    ui_text_center(LCD_H - UI_LINE, hint, UI_GREY);
}

// Shows a message (e.g. "SEARCHING...") without waiting.
static void show_status(const char *title, const char *line1, const char *line2)
{
    ui_clear(UI_BLACK);
    ui_text_center(4, title, UI_YELLOW);
    ui_text_center(52, line1, UI_WHITE);
    if (line2)
        ui_text_center(52 + UI_LINE, line2, UI_WHITE);
    ui_present();
}

// Word-wraps msg into lines of at most 120 px, centred from y.
static void draw_wrapped(int y, const char *msg, uint16_t color)
{
    char line[48];
    int len = 0;
    while (*msg) {
        const char *word_end = msg + strcspn(msg, " ");
        int wlen = word_end - msg;
        char trial[96];
        snprintf(trial, sizeof(trial), "%.*s%s%.*s", len, line, len ? " " : "", wlen, msg);
        if (len && ui_text_width(trial) > 120) {
            line[len] = '\0';
            ui_text_center(y, line, color);
            y += UI_LINE;
            len = 0;
            continue;
        }
        len = snprintf(line, sizeof(line), "%s", trial);
        msg = *word_end ? word_end + 1 : word_end;
    }
    if (len)
        ui_text_center(y, line, color);
}

// ---- name editor ----------------------------------------------------------------

static const char s_charset[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-!";
#define CELL_W 9

// Edits name in place; joystick up/down picks a letter, left/right moves.
// Returns false if cancelled.
static bool edit_name(const char *title, char *name)
{
    char buf[NETPLAY_NAME_LEN + 1];
    memset(buf, ' ', NETPLAY_NAME_LEN);
    buf[NETPLAY_NAME_LEN] = '\0';
    memcpy(buf, name, strlen(name));
    int cur = 0;
    const int x0 = (LCD_W - NETPLAY_NAME_LEN * CELL_W) / 2;

    for (;;) {
        uint16_t b = ui_buttons();
        const char *p = strchr(s_charset, buf[cur]);
        int ci = p ? p - s_charset : 0;
        const int n = sizeof(s_charset) - 1;

        if (b & BTN_UP)
            buf[cur] = s_charset[(ci + 1) % n];
        if (b & BTN_DOWN)
            buf[cur] = s_charset[(ci + n - 1) % n];
        if ((b & BTN_LEFT) && cur > 0)
            cur--;
        if ((b & BTN_RIGHT) && cur < NETPLAY_NAME_LEN - 1)
            cur++;
        if (b & BTN_B)
            return false;
        if (b & (BTN_A | BTN_CENTER)) {
            int len = NETPLAY_NAME_LEN;
            while (len > 0 && buf[len - 1] == ' ')
                len--;
            int start = 0;
            while (start < len && buf[start] == ' ')
                start++;
            if (len > start) {
                memcpy(name, buf + start, len - start);
                name[len - start] = '\0';
            }
            return true;
        }

        ui_clear(UI_BLACK);
        ui_text_center(4, title, UI_YELLOW);
        for (int i = 0; i < NETPLAY_NAME_LEN; i++) {
            char s[2] = {buf[i], 0};
            int x = x0 + i * CELL_W;
            ui_fill(x, 58, CELL_W - 1, 1, i == cur ? UI_YELLOW : UI_DARK);
            ui_text(x + (CELL_W - 1 - ui_text_width(s)) / 2, 49, s, i == cur ? UI_YELLOW : UI_WHITE);
        }
        ui_text_center(80, "UP/DOWN: LETTER", UI_GREY);
        ui_text_center(80 + UI_LINE, "LEFT/RIGHT: MOVE", UI_GREY);
        draw_footer(UI_ICON_A " OK   " UI_ICON_B " CANCEL");
        ui_present();
        wait_frame();
    }
}

// ---- join -----------------------------------------------------------------------

// Lists the games in range, with "search again" as the last entry.
// Returns true once joined.
static bool join_screen(const char **server)
{
    netplay_game_t games[NETPLAY_MAX_GAMES];
    int n = 0, sel = 0;
    bool scan = true;

    for (;;) {
        if (scan) {
            show_status("JOIN GAME", "SEARCHING...", NULL);
            n = netplay_scan(games, NETPLAY_MAX_GAMES);
            sel = 0;
            scan = false;
            ui_buttons(); // drop presses made while scanning
        }

        uint16_t b = ui_buttons();
        if (b & BTN_B)
            return false;
        if (b & BTN_UP)
            sel = (sel + n) % (n + 1);
        if (b & BTN_DOWN)
            sel = (sel + 1) % (n + 1);
        if (b & (BTN_A | BTN_CENTER)) {
            if (sel == n) {
                scan = true;
                continue;
            }
            char line[32];
            snprintf(line, sizeof(line), "\"%s\"", games[sel].name);
            show_status("JOIN GAME", "JOINING", line);
            if (netplay_join(games[sel].name, server))
                return true;
            show_status("JOIN GAME", "COULD NOT JOIN", line);
            vTaskDelay(pdMS_TO_TICKS(2000));
            scan = true;
            continue;
        }

        ui_clear(UI_BLACK);
        ui_text_center(4, "JOIN GAME", UI_YELLOW);
        int y = 20;
        for (int i = 0; i <= n; i++, y += UI_LINE) {
            if (i == sel)
                ui_fill(0, y - 1, LCD_W, UI_LINE, UI_DARK);
            if (i == n) {
                ui_text(8, y, "SEARCH AGAIN", i == sel ? UI_YELLOW : UI_GREY);
                break;
            }
            ui_text(8, y, games[i].name, i == sel ? UI_YELLOW : UI_WHITE);
            // signal strength as 1-4 bars
            int bars = games[i].rssi > -55 ? 4 : games[i].rssi > -67 ? 3 : games[i].rssi > -78 ? 2 : 1;
            for (int k = 0; k < 4; k++)
                ui_fill(104 + k * 4, y + 6 - k * 2, 3, 1 + k * 2, k < bars ? UI_WHITE : UI_DARK);
        }
        if (n == 0) {
            ui_text_center(y + 16, "NO GAMES FOUND", UI_WHITE);
            ui_text_center(y + 16 + UI_LINE, "HOST ONE ON THE", UI_GREY);
            ui_text_center(y + 16 + 2 * UI_LINE, "OTHER CYBERPI", UI_GREY);
        }
        draw_footer(UI_ICON_A " JOIN   " UI_ICON_B " BACK");
        ui_present();
        wait_frame();
    }
}

// ---- main menu ------------------------------------------------------------------

enum { ITEM_SINGLE, ITEM_HOST, ITEM_JOIN, ITEM_NAME, NUM_ITEMS };

launch_mode_t launcher_run(const char **server, void (*before_network)(void))
{
    bool network_ready = false;
    const char *message = netplay_take_message();
    int sel = 0;

    netplay_load_names();
    ui_buttons(); // ignore buttons already held at boot

    for (;;) {
        uint16_t b = ui_buttons();
        if (b)
            message = NULL;
        if (b & BTN_UP)
            sel = (sel + NUM_ITEMS - 1) % NUM_ITEMS;
        if (b & BTN_DOWN)
            sel = (sel + 1) % NUM_ITEMS;
        if (b & (BTN_A | BTN_CENTER)) {
            switch (sel) {
            case ITEM_SINGLE:
                return LAUNCH_SINGLE;
            case ITEM_HOST:
                if (edit_name("NAME YOUR GAME", netplay_game_name)) {
                    if (!network_ready) {
                        before_network();
                        network_ready = true;
                    }
                    netplay_save_names();
                    show_status("HOST GAME", "STARTING...", NULL);
                    if (netplay_host())
                        return LAUNCH_HOST;
                }
                break;
            case ITEM_JOIN:
                if (!network_ready) {
                    before_network();
                    network_ready = true;
                }
                if (join_screen(server))
                    return LAUNCH_JOIN;
                break;
            case ITEM_NAME:
                if (edit_name("YOUR NAME", netplay_player_name))
                    netplay_save_names();
                break;
            }
            continue;
        }

        ui_clear(UI_BLACK);
        int logo_h = ui_logo(4);
        int y = logo_h ? 4 + logo_h + 6 : MENU_Y;
        static const char *const items[NUM_ITEMS] = {"SINGLE PLAYER", "HOST GAME", "JOIN GAME", NULL};
        for (int i = 0; i < NUM_ITEMS; i++, y += UI_LINE + 1) {
            char name_item[32];
            const char *label = items[i];
            if (i == ITEM_NAME) {
                snprintf(name_item, sizeof(name_item), "NAME: %s", netplay_player_name);
                label = name_item;
            }
            if (i == sel)
                ui_fill(0, y - 1, LCD_W, UI_LINE + 1, UI_DARK);
            ui_text_center(y, label, i == sel ? UI_YELLOW : UI_WHITE);
        }
        if (message)
            draw_wrapped(y + 3, message, UI_RED);
        ui_present();
        wait_frame();
    }
}
