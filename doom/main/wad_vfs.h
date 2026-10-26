#pragma once

#include <stddef.h>

#include "esp_err.h"

// Mounts the "wad" data partition read-only at /wad/doom1.wad.
esp_err_t wad_vfs_register(void);

// Maps a byte range of the WAD into the address space (read-only, cached).
// Returns NULL on failure. Mappings are never released.
const void *wad_vfs_mmap(size_t offset, size_t len);
