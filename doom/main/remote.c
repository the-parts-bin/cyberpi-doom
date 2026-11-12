// Remote control over the USB serial port, for testing without touching the
// buttons. One character per command:
//   u d l r c  joystick up/down/left/right/press   a b  A (square) / B (triangle)
//   m          menu                                 capitals hold for a second
//   S          screenshot: "SHOT <base64 of the 128x128 big-endian RGB565 frame>"
#include "remote.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART UART_NUM_0
#define TAP_MS 150
#define HOLD_MS 1000

static const uint16_t *s_fb;

static void send_screenshot(void)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t len = LCD_W * LCD_H * 2;
    uint8_t *p = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!p)
        return;
    memcpy(p, s_fb, len);
    esp_log_level_set("*", ESP_LOG_NONE); // keep log lines out of the data
    char out[256];
    int n = 0;

    uart_write_bytes(UART, "\nSHOT ", 6);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = p[i] << 16 | (i + 1 < len ? p[i + 1] << 8 : 0) | (i + 2 < len ? p[i + 2] : 0);
        out[n++] = b64[v >> 18 & 63];
        out[n++] = b64[v >> 12 & 63];
        out[n++] = i + 1 < len ? b64[v >> 6 & 63] : '=';
        out[n++] = i + 2 < len ? b64[v & 63] : '=';
        if (n > (int)sizeof(out) - 4) {
            uart_write_bytes(UART, out, n);
            n = 0;
        }
    }
    out[n++] = '\n';
    uart_write_bytes(UART, out, n);
    uart_wait_tx_done(UART, portMAX_DELAY);
    esp_log_level_set("*", ESP_LOG_INFO);
    free(p);
}

static void remote_task(void *arg)
{
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART, &c, 1, portMAX_DELAY) != 1)
            continue;
        if (c == 'S') {
            send_screenshot();
            continue;
        }
        uint16_t b = 0;
        switch (tolower(c)) {
        case 'u': b = BTN_UP; break;
        case 'd': b = BTN_DOWN; break;
        case 'l': b = BTN_LEFT; break;
        case 'r': b = BTN_RIGHT; break;
        case 'c': b = BTN_CENTER; break;
        case 'a': b = BTN_A; break;
        case 'b': b = BTN_B; break;
        case 'm': b = BTN_MENU; break;
        }
        if (b)
            board_press(b, isupper(c) ? HOLD_MS : TAP_MS);
    }
}

void remote_init(const uint16_t *fb)
{
    s_fb = fb;
    // With the driver installed, the console keeps working through it.
    if (uart_driver_install(UART, 1024, 0, 0, NULL, 0) != ESP_OK)
        return;
    uart_vfs_dev_use_driver(UART);
    xTaskCreatePinnedToCore(remote_task, "remote", 3072, NULL, 2, NULL, 0);
}
