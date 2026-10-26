// CyberPi board support: AW9523B expanders, ST7735 LCD, RGB LEDs, buttons.
// Pin map from Makeblock's CyberPi-Library-for-Arduino (see HARDWARE.md).
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define LCD_W 128
#define LCD_H 128

// Button bits returned by board_buttons(); set = pressed.
#define BTN_LEFT   (1u << 0)
#define BTN_UP     (1u << 1)
#define BTN_RIGHT  (1u << 2)
#define BTN_CENTER (1u << 3)
#define BTN_DOWN   (1u << 4)
#define BTN_B      (1u << 5)
#define BTN_A      (1u << 6)
#define BTN_MENU   (1u << 8)

void board_init(uint32_t lcd_hz);

// Push a full 128x128 RGB565 frame (big-endian pixels). The buffer must be
// DMA-capable. Blocks until the transfer is done.
void board_lcd_push(const uint16_t *fb);

uint16_t board_buttons(void);
uint16_t board_expander_raw(void);
uint16_t board_led_expander_raw(void);

// rgb: 5 LEDs x {r,g,b}
void board_leds(const uint8_t rgb[15]);
