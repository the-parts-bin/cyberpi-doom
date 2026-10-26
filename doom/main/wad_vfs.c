// Read-only VFS exposing the raw "wad" flash partition as /wad/doom1.wad,
// so Doom's stdio-based WAD loader works unchanged.
#include "wad_vfs.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "esp_vfs.h"

static const char *TAG = "wad_vfs";

#define WAD_MAX_FDS 4

static const esp_partition_t *s_part;
static size_t s_size;
static off_t s_pos[WAD_MAX_FDS];
static bool s_open[WAD_MAX_FDS];

static int wad_open(const char *path, int flags, int mode)
{
    if (strcmp(path, "/doom1.wad") != 0) {
        errno = ENOENT;
        return -1;
    }
    if ((flags & O_ACCMODE) != O_RDONLY) {
        errno = EROFS;
        return -1;
    }
    for (int fd = 0; fd < WAD_MAX_FDS; fd++) {
        if (!s_open[fd]) {
            s_open[fd] = true;
            s_pos[fd] = 0;
            return fd;
        }
    }
    errno = ENFILE;
    return -1;
}

static ssize_t wad_read(int fd, void *dst, size_t size)
{
    if (s_pos[fd] >= s_size)
        return 0;
    if (size > s_size - s_pos[fd])
        size = s_size - s_pos[fd];
    if (esp_partition_read(s_part, s_pos[fd], dst, size) != ESP_OK) {
        errno = EIO;
        return -1;
    }
    s_pos[fd] += size;
    return size;
}

static off_t wad_lseek(int fd, off_t off, int whence)
{
    off_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? s_pos[fd] : (off_t)s_size;
    if (base + off < 0) {
        errno = EINVAL;
        return -1;
    }
    s_pos[fd] = base + off;
    return s_pos[fd];
}

static int wad_fstat(int fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0444;
    st->st_size = s_size;
    return 0;
}

static int wad_close(int fd)
{
    s_open[fd] = false;
    return 0;
}

// The partition is padded, so take the WAD length from its directory:
// the end of the last lump or of the directory itself.
static size_t wad_length(void)
{
    struct {
        char id[4];
        int32_t numlumps, infotableofs;
    } hdr;
    if (esp_partition_read(s_part, 0, &hdr, sizeof(hdr)) != ESP_OK ||
        (memcmp(hdr.id, "IWAD", 4) && memcmp(hdr.id, "PWAD", 4)))
        return 0;

    size_t end = hdr.infotableofs + hdr.numlumps * 16;
    for (int i = 0; i < hdr.numlumps; i++) {
        int32_t lump[2]; // filepos, size
        esp_partition_read(s_part, hdr.infotableofs + i * 16, lump, sizeof(lump));
        if ((size_t)(lump[0] + lump[1]) > end)
            end = lump[0] + lump[1];
    }
    return end;
}

esp_err_t wad_vfs_register(void)
{
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "wad");
    if (!s_part) {
        ESP_LOGE(TAG, "no 'wad' partition");
        return ESP_ERR_NOT_FOUND;
    }
    s_size = wad_length();
    if (!s_size || s_size > s_part->size) {
        ESP_LOGE(TAG, "no valid WAD in partition; flash doom1.wad to 0x%" PRIx32, s_part->address);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "WAD: %u bytes at 0x%" PRIx32, (unsigned)s_size, s_part->address);

    static const esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .open = wad_open,
        .read = wad_read,
        .lseek = wad_lseek,
        .fstat = wad_fstat,
        .close = wad_close,
    };
    return esp_vfs_register("/wad", &vfs, NULL);
}
