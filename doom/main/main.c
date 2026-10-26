// doomgeneric platform layer for the Makeblock CyberPi.
#include <string.h>
#include <sys/stat.h>

#include "board.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hud.h"
#include "d_items.h"
#include "doomstat.h"
#include "esp_vfs_fat.h"
#include "i_video.h"
#include "m_config.h"
#include "m_controls.h"
#include "r_state.h"
#include "wad_vfs.h"

static const char *TAG = "doom";

// Two layouts, both 128 wide (320 -> 128 is exactly 5:2):
// - tall: gameplay, 320x232 -> 128x112 plus a 16-row HUD below. With smaller
//   screen sizes Doom's own status bar also shows, at the bottom of the 232.
//   The extra 32 rows add vertical FOV; the horizontal FOV is unchanged.
// - classic: title, intermission and finale screens are drawn for 320x200,
//   so they keep that layout at 128x96, letterboxed.
// Both preserve Doom's 4:3 aspect (pixels 1.2x taller than wide).
#define OUT_W 128
#define TALL_H (LCD_H - HUD_H)
#define CLASSIC_H 96
#define CLASSIC_Y ((LCD_H - CLASSIC_H) / 2)

// Doom renders on core 1. DG_DrawFrame snapshots the 8-bit frame, palette and
// HUD values into internal RAM and hands them to a core 0 task that scales
// and pushes, dropping the frame if that task is still busy with the last one.
typedef struct {
    bool tall;
    hud_state_t hud;
    uint32_t pal[256];
} frame_info_t;

static uint8_t *s_snap;
static frame_info_t s_snap_info;
static uint32_t s_pal[256];
static SemaphoreHandle_t s_frame_ready;
static volatile bool s_video_busy;

static uint16_t *s_fb;
static uint16_t s_ys_tall[TALL_H + 1], s_ys_classic[CLASSIC_H + 1];
static uint16_t s_recip[10];

static int64_t s_t_snap, s_t_conv, s_t_push, s_t_last_log;
static int s_rendered, s_shown;

// ---- video ------------------------------------------------------------------

// Up to 9 source pixels are summed per output pixel. With 7-bit channels in
// 11-bit lanes the sum (max 9 * 127) can't carry into the next lane.
static uint32_t pack_color(const struct color *c)
{
    return (c->r >> 1) << 22 | (c->g >> 1) << 11 | (c->b >> 1);
}

// Box-filter the 8-bit frame to 128 x rows RGB565 (big-endian for the panel)
// at panel row out_y. Every 5 source pixels become two outputs averaging 2 and
// 3 pixels; each output row sums the 2 or 3 source rows given by ys. Lives in
// IRAM so core 0 never waits on the flash cache while core 1 keeps the SPI bus
// busy with PSRAM traffic.
static IRAM_ATTR void convert_frame(const uint8_t *src, const uint32_t *pal,
                                    const uint16_t *ys, int rows, int out_y)
{
    static uint32_t hsum[OUT_W];
    uint16_t *out = s_fb + out_y * LCD_W;

    for (int y = 0; y < rows; y++) {
        int y0 = ys[y], y1 = ys[y + 1];
        memset(hsum, 0, sizeof(hsum));
        for (int sy = y0; sy < y1; sy++) {
            const uint8_t *p = src + sy * SCREENWIDTH;
            uint32_t *h = hsum;
            for (int i = 0; i < OUT_W / 2; i++, p += 5, h += 2) {
                h[0] += pal[p[0]] + pal[p[1]];
                h[1] += pal[p[2]] + pal[p[3]] + pal[p[4]];
            }
        }

        unsigned n = y1 - y0;
        unsigned k2 = s_recip[2 * n], k3 = s_recip[3 * n];
        for (int x = 0; x < OUT_W; x++) {
            uint32_t acc = hsum[x];
            unsigned k = (x & 1) ? k3 : k2;
            unsigned r = (((acc >> 22) & 0x7FF) * k) >> 16; // 7-bit averages
            unsigned g = (((acc >> 11) & 0x7FF) * k) >> 16;
            unsigned b = ((acc & 0x7FF) * k) >> 16;
            uint16_t c = (r >> 2) << 11 | (g >> 1) << 5 | (b >> 2);
            *out++ = (c >> 8) | (c << 8);
        }
    }
}

static void video_task(void *arg)
{
    bool was_tall = false;
    for (;;) {
        xSemaphoreTake(s_frame_ready, portMAX_DELAY);
        const frame_info_t *f = &s_snap_info;
        int64_t t0 = esp_timer_get_time();
        if (f->tall != was_tall) {
            memset(s_fb, 0, LCD_W * LCD_H * 2); // clear the other layout's leftovers
            was_tall = f->tall;
        }
        if (f->tall) {
            convert_frame(s_snap, f->pal, s_ys_tall, TALL_H, 0);
            hud_draw(s_fb, TALL_H, &f->hud);
        } else {
            convert_frame(s_snap, f->pal, s_ys_classic, CLASSIC_H, CLASSIC_Y);
        }
        int64_t t1 = esp_timer_get_time();
        board_lcd_push(s_fb);
        s_t_conv += t1 - t0;
        s_t_push += esp_timer_get_time() - t1;
        s_shown++;
        s_video_busy = false;
    }
}

// Called before the Doom task exists: the 74 KB snapshot needs one contiguous
// internal block, which the task's stack would otherwise split.
static void video_alloc(void)
{
    s_snap = heap_caps_malloc(SCREENWIDTH * SCREENHEIGHT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_fb = heap_caps_calloc(LCD_W * LCD_H, 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_fb && s_snap);
}

static void video_init(void)
{
    for (int y = 0; y <= TALL_H; y++)
        s_ys_tall[y] = y * SCREENHEIGHT / TALL_H;
    for (int y = 0; y <= CLASSIC_H; y++)
        s_ys_classic[y] = y * 200 / CLASSIC_H;
    for (int n = 1; n < 10; n++)
        s_recip[n] = 65536 / n;
    ESP_LOGI(TAG, "internal heap left: %u bytes", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

    s_frame_ready = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(video_task, "video", 4096, NULL, 5, NULL, 0);
}

static void read_hud(hud_state_t *h)
{
    const player_t *p = &players[consoleplayer];
    ammotype_t t = weaponinfo[p->readyweapon].ammo;
    h->health = p->health;
    h->armor = p->armorpoints;
    h->armor_type = p->armortype;
    h->ammo = t == am_noammo ? -1 : p->ammo[t];
    h->keys = 0;
    for (int k = 0; k < 3; k++)
        if (p->cards[it_bluecard + k] || p->cards[it_blueskull + k])
            h->keys |= 1 << k;
}

void DG_Init(void)
{
}

void DG_DrawFrame(void)
{
    int64_t t0 = esp_timer_get_time();
    s_rendered++;

    if (palette_changed) {
        for (int i = 0; i < 256; i++)
            s_pal[i] = pack_color(&colors[i]);
        palette_changed = false;
    }

    // Doom only writes default.cfg on quit, which never happens on a device
    // that gets switched off; save it whenever the menu closes instead.
    static boolean menu_was_active;
    if (menu_was_active && !menuactive)
        M_SaveDefaults();
    menu_was_active = menuactive;

    if (!s_video_busy) {
        frame_info_t *f = &s_snap_info;
        memcpy(s_snap, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
        memcpy(f->pal, s_pal, sizeof(s_pal));
        f->tall = gamestate == GS_LEVEL;
        if (f->tall)
            read_hud(&f->hud);
        s_video_busy = true;
        xSemaphoreGive(s_frame_ready);
    }

    int64_t t1 = esp_timer_get_time();
    s_t_snap += t1 - t0;
    if (t1 - s_t_last_log > 5000000) {
        float secs = (t1 - s_t_last_log) / 1e6f;
        int shown = s_shown ? s_shown : 1;
        ESP_LOGI(TAG, "render %.1f fps, shown %.1f fps | snapshot %lld us, convert %lld us, push %lld us",
                 s_rendered / secs, s_shown / secs, s_t_snap / s_rendered, s_t_conv / shown, s_t_push / shown);
        s_t_last_log = t1;
        s_t_snap = s_t_conv = s_t_push = 0;
        s_rendered = s_shown = 0;
    }
}

// ---- input ------------------------------------------------------------------
//
// In game:   joystick = move/turn, A = fire, B = use,
//            B + left/right = strafe, B + up/down = previous/next weapon,
//            centre = automap, menu = Escape.
// In menus:  joystick = navigate, A or centre = select, B = back,
//            centre = 'y' on yes/no prompts, menu = Escape.
// The key sent on press is remembered so the release always matches it.

#define KEY_PREVWEAPON '['
#define KEY_NEXTWEAPON ']'

extern int messageToPrint;
extern int screenblocks;
extern boolean messageNeedsInput;

static const uint16_t s_buttons_all[] = {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_CENTER, BTN_MENU,
};
#define NUM_BUTTONS (sizeof(s_buttons_all) / sizeof(s_buttons_all[0]))

static unsigned char key_for(uint16_t bit, uint16_t held)
{
    bool in_menu = menuactive || gamestate != GS_LEVEL;
    bool prompt = messageToPrint && messageNeedsInput;
    bool mod = !in_menu && (held & BTN_B);

    switch (bit) {
    case BTN_UP: return mod ? KEY_NEXTWEAPON : KEY_UPARROW;
    case BTN_DOWN: return mod ? KEY_PREVWEAPON : KEY_DOWNARROW;
    case BTN_LEFT: return mod ? KEY_STRAFE_L : KEY_LEFTARROW;
    case BTN_RIGHT: return mod ? KEY_STRAFE_R : KEY_RIGHTARROW;
    case BTN_A: return in_menu ? KEY_ENTER : KEY_FIRE;
    case BTN_B: return in_menu ? KEY_BACKSPACE : KEY_USE;
    case BTN_CENTER: return prompt ? 'y' : in_menu ? KEY_ENTER : KEY_TAB;
    case BTN_MENU: return KEY_ESCAPE;
    }
    return 0;
}

#define KEYQ_LEN 16
static uint16_t s_keyq[KEYQ_LEN]; // (pressed << 8) | key
static int s_keyq_head, s_keyq_tail;
static uint16_t s_buttons;
static unsigned char s_sent[NUM_BUTTONS];
static bool s_polled;

static void push_key(int pressed, unsigned char key)
{
    s_keyq[s_keyq_head] = (pressed << 8) | key;
    s_keyq_head = (s_keyq_head + 1) % KEYQ_LEN;
}

int DG_GetKey(int *pressed, unsigned char *key)
{
    // Poll once per event drain; doomgeneric calls this until it returns 0.
    if (s_keyq_head == s_keyq_tail && !s_polled) {
        uint16_t now = board_buttons(), changed = now ^ s_buttons;
        for (int i = 0; i < NUM_BUTTONS; i++) {
            uint16_t bit = s_buttons_all[i];
            if (!(changed & bit))
                continue;
            if (now & bit) {
                s_sent[i] = key_for(bit, now);
                push_key(1, s_sent[i]);
            } else {
                push_key(0, s_sent[i]);
            }
        }
        s_buttons = now;
        s_polled = true;
    }
    if (s_keyq_head == s_keyq_tail) {
        s_polled = false;
        return 0;
    }
    *pressed = s_keyq[s_keyq_tail] >> 8;
    *key = s_keyq[s_keyq_tail] & 0xFF;
    s_keyq_tail = (s_keyq_tail + 1) % KEYQ_LEN;
    return 1;
}

// ---- storage ----------------------------------------------------------------

// Config and savegames live on a FAT partition at /data (see DG_CONFIG_DIR).
static void storage_init(void)
{
    static wl_handle_t wl;
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl("/data", "storage", &cfg, &wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mounting /data failed (%s); config and saves disabled", esp_err_to_name(err));
        return;
    }
    mkdir("/data/savegame", 0755);
}

// ---- system -----------------------------------------------------------------

void DG_SleepMs(uint32_t ms)
{
    if (ms)
        vTaskDelay(pdMS_TO_TICKS(ms));
    else
        taskYIELD();
}

uint32_t DG_GetTicksMs(void)
{
    return esp_timer_get_time() / 1000;
}

void DG_SetWindowTitle(const char *title)
{
}

static void doom_task(void *arg)
{
    board_init(40 * 1000 * 1000);
    video_init();
    ESP_ERROR_CHECK(wad_vfs_register());
    storage_init();

    // Defaults for a first run; default.cfg overrides them afterwards.
    joybspeed = 29; // >= MAX_JOY_BUTTONS: always run
    key_prevweapon = KEY_PREVWEAPON;
    key_nextweapon = KEY_NEXTWEAPON;
    screenblocks = 11; // full-screen view: the tall layout with our HUD

    static char *argv[] = {"doom", "-iwad", "/wad/doom1.wad", "-mb", "3", NULL};
    doomgeneric_Create(5, argv);
    for (;;)
        doomgeneric_Tick();
}

void app_main(void)
{
    video_alloc();
    xTaskCreatePinnedToCore(doom_task, "doom", 32 * 1024, NULL, 5, NULL, 1);
}
