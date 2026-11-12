#include "ui.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#define WAD_PATH "/wad/doom1.wad"
#define FONT_FIRST '!'
#define FONT_LAST '_'
#define SPACE_W 4

static uint16_t *s_fb;
static uint8_t s_pal[256][3];
static uint8_t *s_font[FONT_LAST - FONT_FIRST + 1]; // Doom patches
static uint8_t *s_logo;

#define BE(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

// ---- WAD ----------------------------------------------------------------------

typedef struct {
    int32_t pos, size;
    char name[8];
} wad_lump_t;

static uint8_t *read_lump(FILE *f, const wad_lump_t *dir, int n, const char *name)
{
    for (int i = 0; i < n; i++) {
        if (strncmp(dir[i].name, name, 8) != 0)
            continue;
        // PSRAM: internal RAM is needed for Wi-Fi
        uint8_t *data = heap_caps_malloc(dir[i].size, MALLOC_CAP_SPIRAM);
        if (data && (fseek(f, dir[i].pos, SEEK_SET) != 0 || fread(data, 1, dir[i].size, f) != dir[i].size)) {
            free(data);
            data = NULL;
        }
        return data;
    }
    return NULL;
}

void ui_init(uint16_t *fb)
{
    s_fb = fb;

    FILE *f = fopen(WAD_PATH, "rb");
    if (!f)
        return;
    int32_t hdr[3];
    if (fread(hdr, 4, 3, f) == 3) {
        int n = hdr[1];
        wad_lump_t *dir = heap_caps_malloc(n * sizeof(*dir), MALLOC_CAP_SPIRAM);
        if (dir && fseek(f, hdr[2], SEEK_SET) == 0 && fread(dir, sizeof(*dir), n, f) == n) {
            uint8_t *pal = read_lump(f, dir, n, "PLAYPAL");
            if (pal) {
                memcpy(s_pal, pal, sizeof(s_pal));
                free(pal);
            }
            for (int c = FONT_FIRST; c <= FONT_LAST; c++) {
                char name[9];
                snprintf(name, sizeof(name), "STCFN%03d", c);
                s_font[c - FONT_FIRST] = read_lump(f, dir, n, name);
            }
            s_logo = read_lump(f, dir, n, "M_DOOM");
        }
        free(dir);
    }
    fclose(f);
}

// ---- drawing ------------------------------------------------------------------

static inline int16_t rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static inline int32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; }

static void put(int x, int y, uint16_t c)
{
    if ((unsigned)x < LCD_W && (unsigned)y < LCD_H)
        s_fb[y * LCD_W + x] = BE(c);
}

// Calls fn for each opaque pixel of a Doom patch (column posts).
#define FOR_PATCH_PIXELS(patch, X, Y, IDX, ...)                                      \
    do {                                                                             \
        int w_ = rd16(patch);                                                        \
        for (int X = 0; X < w_; X++) {                                               \
            const uint8_t *col_ = (patch) + rd32((patch) + 8 + X * 4);               \
            while (col_[0] != 0xFF) {                                                \
                int top_ = col_[0], len_ = col_[1];                                  \
                for (int k_ = 0; k_ < len_; k_++) {                                  \
                    int Y = top_ + k_;                                               \
                    int IDX = col_[3 + k_];                                          \
                    __VA_ARGS__;                                                            \
                }                                                                    \
                col_ += len_ + 4;                                                    \
            }                                                                        \
        }                                                                            \
    } while (0)

void ui_clear(uint16_t color)
{
    ui_fill(0, 0, LCD_W, LCD_H, color);
}

void ui_fill(int x, int y, int w, int h, uint16_t color)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(i, j, color);
}

static const uint8_t *glyph(char c)
{
    if (c >= 'a' && c <= 'z')
        c -= 'a' - 'A';
    if (c < FONT_FIRST || c > FONT_LAST)
        return NULL;
    return s_font[c - FONT_FIRST];
}

// The CyberPi's A and B buttons are marked with a square and a triangle.
#define ICON_W 9

static bool is_icon(char c)
{
    return c == UI_ICON_A[0] || c == UI_ICON_B[0];
}

static void draw_icon(int x, int y, char c, uint16_t color)
{
    if (c == UI_ICON_A[0]) {
        ui_fill(x + 1, y, 6, 6, color);
    } else {
        for (int row = 0; row < 7; row++) { // 1, 1, 3, 3, 5, 5, 7 wide
            int w = (row / 2) * 2 + 1;
            ui_fill(x + 4 - w / 2, y + row - 1, w, 1, color);
        }
    }
}

int ui_text_width(const char *s)
{
    int w = 0;
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        w += is_icon(*s) ? ICON_W : g ? rd16(g) : SPACE_W;
    }
    return w;
}

// The font is drawn in shades of red; keep each pixel's brightness and
// apply the requested colour instead.
int ui_text(int x, int y, const char *s, uint16_t color)
{
    int r = (color >> 11) << 3, g = ((color >> 5) & 0x3F) << 2, b = (color & 0x1F) << 3;
    for (; *s; s++) {
        if (is_icon(*s)) {
            draw_icon(x, y, *s, color);
            x += ICON_W;
            continue;
        }
        const uint8_t *p = glyph(*s);
        if (!p) {
            x += SPACE_W;
            continue;
        }
        int ox = x - rd16(p + 4), oy = y - rd16(p + 6); // patch offsets
        FOR_PATCH_PIXELS(p, px, py, idx, {
            const uint8_t *c = s_pal[idx];
            int v = c[0] > c[1] ? c[0] : c[1];
            v = v > c[2] ? v : c[2];
            v = v * 3 / 2 > 255 ? 255 : v * 3 / 2; // the font tops out around 170
            put(ox + px, oy + py, UI_RGB(r * v / 255, g * v / 255, b * v / 255));
        });
        x += rd16(p);
    }
    return x;
}

void ui_text_center(int y, const char *s, uint16_t color)
{
    ui_text((LCD_W - ui_text_width(s)) / 2, y, s, color);
}

// Box-filters the patch 2:1 in both directions; a 2x2 cell is drawn if at
// least two of its pixels are opaque.
int ui_logo(int y)
{
    if (!s_logo)
        return 0;
    int w = rd16(s_logo), h = rd16(s_logo + 2);
    int ow = w / 2, oh = h / 2;
    uint16_t (*acc)[4] = heap_caps_calloc(ow * oh, sizeof(*acc), MALLOC_CAP_SPIRAM); // r, g, b, count
    if (!acc)
        return 0;
    FOR_PATCH_PIXELS(s_logo, px, py, idx, {
        int ox = px / 2, oy = py / 2;
        if (ox < ow && oy < oh) {
            uint16_t *a = acc[oy * ow + ox];
            a[0] += s_pal[idx][0];
            a[1] += s_pal[idx][1];
            a[2] += s_pal[idx][2];
            a[3]++;
        }
    });
    int x0 = (LCD_W - ow) / 2;
    for (int j = 0; j < oh; j++)
        for (int i = 0; i < ow; i++) {
            const uint16_t *a = acc[j * ow + i];
            if (a[3] >= 2)
                put(x0 + i, y + j, UI_RGB(a[0] / a[3], a[1] / a[3], a[2] / a[3]));
        }
    free(acc);
    return oh;
}

void ui_present(void)
{
    board_lcd_push(s_fb);
}

// ---- input --------------------------------------------------------------------

#define REPEAT_DELAY_US 350000
#define REPEAT_RATE_US 110000
#define REPEATING (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)

uint16_t ui_buttons(void)
{
    static uint16_t prev;
    static int64_t next_repeat;

    uint16_t now = board_buttons();
    uint16_t pressed = now & ~prev;
    int64_t t = esp_timer_get_time();

    if (pressed & REPEATING)
        next_repeat = t + REPEAT_DELAY_US;
    else if ((now & REPEATING) && t >= next_repeat) {
        pressed |= now & REPEATING;
        next_repeat = t + REPEAT_RATE_US;
    }
    prev = now;
    return pressed;
}
