#pragma once

#include <stdint.h>

#define HUD_H 16

typedef struct {
    int health;
    int armor;
    int armor_type; // 1 = green, 2 = blue
    int ammo;       // -1 for weapons without ammo
    uint8_t keys;   // bit 0..2 = blue/yellow/red (card or skull)
} hud_state_t;

// Draws the HUD into rows [y, y + HUD_H) of a 128-wide big-endian RGB565 buffer.
void hud_draw(uint16_t *fb, int y, const hud_state_t *s);
