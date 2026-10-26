#pragma once

#include "esp_err.h"

// Mounts the "wad" data partition read-only at /wad/doom1.wad.
esp_err_t wad_vfs_register(void);
