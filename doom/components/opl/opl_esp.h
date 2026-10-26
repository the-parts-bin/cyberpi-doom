// OPL software driver for ESP32: DBOPL rendered from the platform's audio task.
#pragma once

#include <stdint.h>

#define OPL_ESP_RATE 22050

// Adds n mono samples of OPL output (roughly 16-bit range) to out.
// Does nothing until the music module has initialised the driver.
void OPL_ESP_Render(int32_t *out, unsigned int n);
