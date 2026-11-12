// Simple screens drawn outside the game (launcher, network lobby), in Doom's
// small font read straight from the WAD.
#pragma once

#include <stdint.h>

#define UI_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define UI_LINE 9 // text line pitch in pixels

#define UI_WHITE  UI_RGB(235, 235, 235)
#define UI_GREY   UI_RGB(130, 130, 130)
#define UI_DARK   UI_RGB(40, 40, 40)
#define UI_RED    UI_RGB(230, 50, 40)
#define UI_YELLOW UI_RGB(250, 210, 60)
#define UI_BLACK  0

// Button icons for use in strings: A is a square, B a triangle on the case.
#define UI_ICON_A "\x01"
#define UI_ICON_B "\x02"

// Loads the font and palette; fb is the 128x128 big-endian RGB565 buffer the
// screens are drawn into.
void ui_init(uint16_t *fb);

void ui_clear(uint16_t color);
void ui_fill(int x, int y, int w, int h, uint16_t color);
// Halves the brightness of a rectangle, as a backdrop for text.
void ui_darken(int x, int y, int w, int h);
int ui_text_width(const char *s);
// Draws s (upper-cased) with its top at y, returns the x after it.
int ui_text(int x, int y, const char *s, uint16_t color);
void ui_text_center(int y, const char *s, uint16_t color);
// The DOOM logo at half size, centred, top at y; returns its height.
int ui_logo(int y);
void ui_present(void);

// Buttons newly pressed since the last call; held joystick directions
// repeat. Uses the BTN_* bits from board.h.
uint16_t ui_buttons(void);
