// Hardware bring-up: orientation pattern, live button boxes, RGB LEDs,
// and LCD push timing logged over serial.
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hwtest";

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define BE(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))

static uint16_t *fb;

static void rect(int x, int y, int w, int h, uint16_t c)
{
    c = BE(c);
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            fb[j * LCD_W + i] = c;
}

static void draw(uint16_t btn, int frame)
{
    // Moving gradient background, so tearing / dropped frames are visible
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            fb[y * LCD_W + x] = BE(RGB565(x * 2, ((y + frame) & 127) * 2, 64));

    // 1px white border: checks the panel offset
    rect(0, 0, LCD_W, 1, 0xFFFF);
    rect(0, LCD_H - 1, LCD_W, 1, 0xFFFF);
    rect(0, 0, 1, LCD_H, 0xFFFF);
    rect(LCD_W - 1, 0, 1, LCD_H, 0xFFFF);

    // Corners: red top-left, green top-right, blue bottom-left, yellow bottom-right
    rect(2, 2, 12, 12, RGB565(255, 0, 0));
    rect(LCD_W - 14, 2, 12, 12, RGB565(0, 255, 0));
    rect(2, LCD_H - 14, 12, 12, RGB565(0, 0, 255));
    rect(LCD_W - 14, LCD_H - 14, 12, 12, RGB565(255, 255, 0));

    // Joystick cross in the middle, A/B on the right, menu at the top
    uint16_t on = RGB565(255, 255, 255), off = RGB565(40, 40, 40);
    rect(44, 34, 16, 16, btn & BTN_UP ? on : off);
    rect(44, 74, 16, 16, btn & BTN_DOWN ? on : off);
    rect(24, 54, 16, 16, btn & BTN_LEFT ? on : off);
    rect(64, 54, 16, 16, btn & BTN_RIGHT ? on : off);
    rect(44, 54, 16, 16, btn & BTN_CENTER ? on : off);
    rect(96, 44, 16, 16, btn & BTN_A ? RGB565(255, 80, 80) : off);
    rect(96, 68, 16, 16, btn & BTN_B ? RGB565(80, 80, 255) : off);
    rect(56, 18, 16, 8, btn & BTN_MENU ? RGB565(80, 255, 80) : off);
}

void app_main(void)
{
    board_init(40 * 1000 * 1000);

    fb = heap_caps_malloc(LCD_W * LCD_H * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(fb);

    // Diagnostic: watch every free input-capable GPIO for the menu button
    static const int probe[] = {0, 5, 13, 14, 15, 21, 22, 23, 25, 26, 27, 32, 33, 34, 35, 36, 37, 38, 39};
    for (int i = 0; i < sizeof(probe) / sizeof(probe[0]); i++)
        gpio_input_enable(probe[i]);
    uint16_t last_raw = 0;
    uint64_t last_gpio = 0;

    uint16_t last = 0xFFFF;
    int64_t push_us = 0, t_log = esp_timer_get_time();
    for (int frame = 0;; frame++) {
        uint16_t raw = board_expander_raw();
        uint16_t raw_led = board_led_expander_raw();
        static uint16_t last_raw_led = 0xFFFF;
        if (raw_led != last_raw_led) {
            ESP_LOGI(TAG, "raw led-aw=0x%04x", raw_led);
            last_raw_led = raw_led;
        }
        uint64_t gpio = 0;
        for (int i = 0; i < sizeof(probe) / sizeof(probe[0]); i++)
            gpio |= (uint64_t)gpio_get_level(probe[i]) << probe[i];
        if (raw != last_raw || gpio != last_gpio) {
            ESP_LOGI(TAG, "raw aw=0x%04x gpio=0x%010llx", raw, gpio);
            last_raw = raw;
            last_gpio = gpio;
        }

        uint16_t btn = board_buttons();
        if (btn != last) {
            ESP_LOGI(TAG, "buttons 0x%03x", btn);
            uint8_t led[15] = {0};
            for (int i = 0; i < 5; i++) {
                led[i * 3 + 0] = btn & BTN_A ? 40 : 0;
                led[i * 3 + 1] = btn & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_CENTER) ? 40 : 0;
                led[i * 3 + 2] = btn & BTN_B ? 40 : 0;
            }
            board_leds(led);
            last = btn;
        }

        draw(btn, frame);
        int64_t t0 = esp_timer_get_time();
        board_lcd_push(fb);
        push_us += esp_timer_get_time() - t0;

        int64_t now = esp_timer_get_time();
        if (now - t_log > 2000000) {
            ESP_LOGI(TAG, "frame %d, avg push %lld us", frame, push_us / (frame + 1));
            t_log = now;
        }
    }
}
