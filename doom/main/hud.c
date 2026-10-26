// Compact HUD for the 128-pixel-wide panel: Doom's own status bar is
// unreadable at 2.5x reduction, so draw health/armor/ammo/keys natively.
#include "hud.h"

#include "board.h"

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define BE(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

#define COL_BG     RGB565(24, 24, 24)
#define COL_TEXT   RGB565(230, 230, 230)
#define COL_LOW    RGB565(255, 60, 60)
#define COL_HEALTH RGB565(220, 40, 40)
#define COL_GREEN  RGB565(60, 200, 60)
#define COL_BLUE   RGB565(70, 110, 255)
#define COL_AMMO   RGB565(230, 190, 60)
#define COL_YELLOW RGB565(240, 220, 40)

// 3x5 digits, one row per byte, MSB-first in the low 3 bits
static const uint8_t s_digits[11][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
    {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1},
    {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 7, 0, 0}, // '-'
};

// 7x7 icons
static const uint8_t s_icon_health[7] = {0x1C, 0x1C, 0x7F, 0x7F, 0x7F, 0x1C, 0x1C};
static const uint8_t s_icon_armor[7] = {0x7F, 0x7F, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t s_icon_ammo[7] = {0x08, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x3E};

static uint16_t *s_fb;

static void fill(int x, int y, int w, int h, uint16_t c)
{
    c = BE(c);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            s_fb[j * LCD_W + i] = c;
}

static void bitmap(int x, int y, const uint8_t *rows, int w, int h, int scale, uint16_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (rows[j] & (1 << (w - 1 - i)))
                fill(x + i * scale, y + j * scale, scale, scale, c);
}

// Right-aligned in a 3-digit field (20 px wide at scale 2).
static void number(int x, int y, int v, uint16_t c)
{
    if (v < 0) {
        bitmap(x + 14, y, s_digits[10], 3, 5, 2, c);
        return;
    }
    if (v > 999)
        v = 999;
    int pos = 2;
    do {
        bitmap(x + pos * 7, y, s_digits[v % 10], 3, 5, 2, c);
        v /= 10;
        pos--;
    } while (v && pos >= 0);
}

void hud_draw(uint16_t *fb, int y, const hud_state_t *s)
{
    s_fb = fb;
    fill(0, y, LCD_W, HUD_H, COL_BG);

    int ty = y + 3, iy = y + 4;

    bitmap(2, iy, s_icon_health, 7, 7, 1, COL_HEALTH);
    number(11, ty, s->health, s->health <= 25 ? COL_LOW : COL_TEXT);

    bitmap(36, iy, s_icon_armor, 7, 7, 1, s->armor_type == 2 ? COL_BLUE : COL_GREEN);
    number(45, ty, s->armor, COL_TEXT);

    bitmap(70, iy, s_icon_ammo, 7, 7, 1, COL_AMMO);
    number(79, ty, s->ammo, COL_TEXT);

    static const uint16_t key_col[3] = {COL_BLUE, COL_YELLOW, COL_LOW};
    for (int k = 0; k < 3; k++)
        if (s->keys & (1 << k))
            fill(105 + k * 7, ty, 5, 10, key_col[k]);
}
