// Remote control and screenshots over the USB serial port, for testing.
#pragma once

#include <stdint.h>

// fb is the panel frame buffer that screenshots are taken from.
void remote_init(const uint16_t *fb);
