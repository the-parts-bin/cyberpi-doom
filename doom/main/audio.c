// Sound for doomgeneric: sound effects plus OPL music, mixed to 22.05 kHz mono
// on core 0 and played through the ESP32's DAC (GPIO25) into the speaker amp.
//
// Effects play straight from flash: the DS* lumps are contiguous in the WAD,
// so their span is memory-mapped once instead of cached in Doom's zone, whose
// purging would race with the mixer on the other core.
#include "audio.h"

#include <math.h>
#include <string.h>
#include <strings.h>

#include "board.h"
#include "driver/dac_continuous.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i_sound.h"
#include "opl_esp.h"
#include "w_wad.h"
#include "wad_vfs.h"

static const char *TAG = "audio";

extern int musicVolume; // s_sound.c, menu scale 0..15

#define RATE OPL_ESP_RATE
#define BLOCK 256 // samples per DAC write, ~11.6 ms
#define NUM_CHANNELS 16

// The mix is scaled by 1/128 into the 8-bit DAC. A full-volume effect alone
// would span the whole range; scale effects to a quarter of that so
// overlapping effects and music have headroom.
#define SFX_ATTEN 2

// DBOPL's full-volume peaks (~8600) then sit just under the DAC's range.
#define MUSIC_GAIN 2

// The speaker is so loud that comfortable levels use only a few of the DAC's
// 256 steps, and plain rounding noise drowns quiet music. So the DAC runs 16x
// faster and a 2nd-order error-feedback loop pushes the rounding noise above
// the audio band: ~35 dB less below 20 kHz than plain rounding, simulated on
// rendered music. No dither: it measured as the main source of hiss.
#define OSR 16
#define OSR_SHIFT 4
#define DAC_RATE (RATE * OSR)

typedef struct {
    const uint8_t *samples; // unsigned 8-bit
    uint32_t end;           // length, 16.16
    uint32_t pos;           // 16.16
    uint32_t step;          // 16.16 source samples per output sample
    int vol;                // 0..127
    bool active;
} sfx_channel_t;

static sfx_channel_t s_chan[NUM_CHANNELS];
static portMUX_TYPE s_chan_lock = portMUX_INITIALIZER_UNLOCKED;

static const uint8_t *s_sfx_map;
static int s_sfx_lo, s_sfx_hi;
static boolean s_use_prefix;

static int64_t s_t_mix, s_music_sq;
static int s_music_peak;
static int s_blocks;

// ---- mixer ------------------------------------------------------------------

// Interpolates one block (mix / 128 = DAC steps) to OSR x the rate and
// quantizes it to unsigned 8-bit with noise shaping. Works in Q8 steps.
static void shape_block(const int32_t *mix, uint8_t *out)
{
    static int32_t prev, e1, e2;

    // In silence the loop would keep toggling the lowest bit, which this
    // speaker makes audible; output a flat mid level instead.
    bool silent = true;
    for (int i = 0; i < BLOCK && silent; i++)
        silent = mix[i] == 0;
    if (silent) {
        memset(out, 128, BLOCK * OSR);
        prev = e1 = e2 = 0;
        return;
    }

    for (int i = 0; i < BLOCK; i++) {
        int32_t x = mix[i] * 2; // mix / 128 in Q8
        int32_t d = x - prev;
        for (int j = 1; j <= OSR; j++) {
            int32_t xi = prev + ((d * j) >> OSR_SHIFT);
            int32_t v = xi - (2 * e1 - e2);
            int32_t q = (v + 128) >> 8;
            q = q < -128 ? -128 : q > 127 ? 127 : q;
            int32_t e = (q << 8) - v;
            e = e < -1024 ? -1024 : e > 1024 ? 1024 : e; // stay stable when clipping
            e2 = e1;
            e1 = e;
            *out++ = q + 128;
        }
        prev = x;
    }
}

static void mix_sfx(int32_t *out)
{
    portENTER_CRITICAL(&s_chan_lock);
    for (int c = 0; c < NUM_CHANNELS; c++) {
        sfx_channel_t *ch = &s_chan[c];
        if (!ch->active)
            continue;
        uint32_t pos = ch->pos;
        int vol = ch->vol;
        for (int i = 0; i < BLOCK && pos < ch->end; i++, pos += ch->step)
            out[i] += ((ch->samples[pos >> 16] - 128) * vol) >> SFX_ATTEN;
        ch->pos = pos;
        if (pos >= ch->end)
            ch->active = false;
    }
    portEXIT_CRITICAL(&s_chan_lock);
}

static void audio_task(void *arg)
{
    dac_continuous_handle_t dac;
    const dac_continuous_config_t cfg = {
        .chan_mask = DAC_CHANNEL_MASK_CH0, // GPIO25
        // On the ESP32 each sample takes 16 bits of DMA, and one descriptor
        // holds at most ~4 KB, so split each block over several.
        .desc_num = 8,
        .buf_size = 2048,
        .freq_hz = DAC_RATE,
        .offset = 0,
        .clk_src = DAC_DIGI_CLK_SRC_APLL,
        .chan_mode = DAC_CHANNEL_MODE_SIMUL,
    };
    ESP_ERROR_CHECK(dac_continuous_new_channels(&cfg, &dac));
    ESP_ERROR_CHECK(dac_continuous_enable(dac));

    static int32_t mix[BLOCK];
    static uint8_t out[BLOCK * OSR];
    for (;;) {
        int64_t t0 = esp_timer_get_time();
        memset(mix, 0, sizeof(mix));
        OPL_ESP_Render(mix, BLOCK);
        for (int i = 0; i < BLOCK; i++) {
            int a = mix[i] < 0 ? -mix[i] : mix[i];
            s_music_peak = a > s_music_peak ? a : s_music_peak;
            s_music_sq += (int64_t)mix[i] * mix[i];
        }
        // Music must still be rendered to keep its timing running, but at
        // volume 0 it is dropped so nothing can leak through.
        if (musicVolume == 0)
            memset(mix, 0, sizeof(mix));
        else
            for (int i = 0; i < BLOCK; i++)
                mix[i] *= MUSIC_GAIN;
        mix_sfx(mix);
        shape_block(mix, out);
        s_t_mix += esp_timer_get_time() - t0;
        if (++s_blocks == RATE / BLOCK * 10) { // ~10 s
            ESP_LOGI(TAG, "mixing uses %.1f%% of core 0; music peak %d, rms %d",
                     100.0 * s_t_mix / (s_blocks * BLOCK * 1e6 / RATE), s_music_peak,
                     (int)sqrtf((float)s_music_sq / (s_blocks * BLOCK)));
            s_t_mix = 0;
            s_music_peak = 0;
            s_music_sq = 0;
            s_blocks = 0;
        }
        dac_continuous_write(dac, out, sizeof(out), NULL, -1);
    }
}

void audio_init(void)
{
    board_amp(true);
    // Above the video task, so the DAC never runs dry.
    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 0);
}

// ---- doomgeneric sound module ---------------------------------------------------

static boolean Audio_Init(boolean use_sfx_prefix)
{
    s_use_prefix = use_sfx_prefix;

    s_sfx_lo = -1;
    for (unsigned i = 0; i < numlumps; i++) {
        if (strncasecmp(lumpinfo[i].name, "DS", 2) != 0)
            continue;
        if (s_sfx_lo < 0 || lumpinfo[i].position < s_sfx_lo)
            s_sfx_lo = lumpinfo[i].position;
        if (lumpinfo[i].position + lumpinfo[i].size > s_sfx_hi)
            s_sfx_hi = lumpinfo[i].position + lumpinfo[i].size;
    }
    if (s_sfx_lo < 0)
        return false;

    s_sfx_map = wad_vfs_mmap(s_sfx_lo, s_sfx_hi - s_sfx_lo);
    if (!s_sfx_map) {
        ESP_LOGE(TAG, "can't map sound effects");
        return false;
    }
    ESP_LOGI(TAG, "sound effects mapped: %d KB", (s_sfx_hi - s_sfx_lo) / 1024);
    return true;
}

static void Audio_Shutdown(void)
{
}

static int Audio_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];

    if (sfx->link != NULL)
        sfx = sfx->link;
    snprintf(name, sizeof(name), s_use_prefix ? "ds%s" : "%s", sfx->name);
    return W_GetNumForName(name);
}

static void Audio_Update(void)
{
}

static void Audio_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < NUM_CHANNELS)
        s_chan[channel].vol = vol; // mono speaker: separation is ignored
}

static int Audio_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    const lumpinfo_t *l = &lumpinfo[sfx->lumpnum];
    if (l->position < s_sfx_lo || l->position + l->size > s_sfx_hi)
        return -1;

    // DMX header: format 3, rate, length; DMX skips 16 bytes at each end.
    const uint8_t *d = s_sfx_map + (l->position - s_sfx_lo);
    uint32_t rate = d[2] | d[3] << 8;
    uint32_t len = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
    if (l->size < 8 || d[0] != 3 || d[1] != 0 || len > l->size - 8 || len <= 48)
        return -1;

    portENTER_CRITICAL(&s_chan_lock);
    sfx_channel_t *ch = &s_chan[channel];
    ch->samples = d + 8 + 16;
    ch->end = (len - 32) << 16;
    ch->pos = 0;
    ch->step = (rate << 16) / RATE;
    ch->vol = vol;
    ch->active = true;
    portEXIT_CRITICAL(&s_chan_lock);
    return channel;
}

static void Audio_StopSound(int channel)
{
    if (channel >= 0 && channel < NUM_CHANNELS)
        s_chan[channel].active = false;
}

static boolean Audio_SoundIsPlaying(int channel)
{
    return channel >= 0 && channel < NUM_CHANNELS && s_chan[channel].active;
}

static void Audio_CacheSounds(sfxinfo_t *sounds, int num_sounds)
{
}

static snddevice_t s_sound_devices[] = {
    SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS,
    SNDDEVICE_WAVEBLASTER, SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32,
};

sound_module_t DG_sound_module = {
    s_sound_devices,
    sizeof(s_sound_devices) / sizeof(s_sound_devices[0]),
    Audio_Init,
    Audio_Shutdown,
    Audio_GetSfxLumpNum,
    Audio_Update,
    Audio_UpdateSoundParams,
    Audio_StartSound,
    Audio_StopSound,
    Audio_SoundIsPlaying,
    Audio_CacheSounds,
};
