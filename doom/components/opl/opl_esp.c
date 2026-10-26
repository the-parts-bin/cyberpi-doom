// OPL driver for ESP32, adapted from Chocolate Doom's opl_sdl.c: instead of an
// SDL mixer hook, the platform's audio task pulls samples with OPL_ESP_Render.
// Callbacks scheduled by the music code run at exact sample positions inside
// that task, with FreeRTOS mutexes in place of the SDL ones.

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "dbopl.h"
#include "opl.h"
#include "opl_esp.h"
#include "opl_internal.h"
#include "opl_queue.h"

#define MAX_BLOCK 512

typedef struct
{
    unsigned int rate;        // Number of times the timer is advanced per sec.
    unsigned int enabled;     // Non-zero if timer is enabled.
    unsigned int value;       // Last value that was set.
    uint64_t expire_time;     // Calculated time that timer will expire.
} opl_timer_t;

// Held while a callback runs; OPL_Lock takes it to keep callbacks out.
static SemaphoreHandle_t callback_mutex;

// Protects the callback queue and current_time.
static SemaphoreHandle_t callback_queue_mutex;

// Keeps register writes from Doom's thread out of a block being generated.
static SemaphoreHandle_t chip_mutex;

static opl_callback_queue_t *callback_queue;

// Current time, in us since startup.
static uint64_t current_time;

static int opl_paused;

// Time offset (in us) due to the fact that callbacks were previously paused.
static uint64_t pause_offset;

static Chip opl_chip;
static Bit32s mix_buffer[MAX_BLOCK];
static volatile bool initialized;

static int register_num = 0;

// Timers; DBOPL does not do timer stuff itself.
static opl_timer_t timer1 = { 12500, 0, 0, 0 };
static opl_timer_t timer2 = { 3125, 0, 0, 0 };

// Advance time by the specified number of samples, invoking any
// callback functions as appropriate.
static void AdvanceTime(unsigned int nsamples)
{
    opl_callback_t callback;
    void *callback_data;
    uint64_t us;

    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);

    us = ((uint64_t) nsamples * OPL_SECOND) / OPL_ESP_RATE;
    current_time += us;

    if (opl_paused)
    {
        pause_offset += us;
    }

    while (!OPL_Queue_IsEmpty(callback_queue)
        && current_time >= OPL_Queue_Peek(callback_queue) + pause_offset)
    {
        if (!OPL_Queue_Pop(callback_queue, &callback, &callback_data))
        {
            break;
        }

        // Hold callback_mutex (not callback_queue_mutex) while invoking, so
        // OPL_Lock can block callbacks and callbacks can schedule new ones.

        xSemaphoreGive(callback_queue_mutex);

        xSemaphoreTake(callback_mutex, portMAX_DELAY);
        callback(callback_data);
        xSemaphoreGive(callback_mutex);

        xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    }

    xSemaphoreGive(callback_queue_mutex);
}

void OPL_ESP_Render(int32_t *out, unsigned int n)
{
    unsigned int filled = 0;

    if (!initialized)
    {
        return;
    }

    while (filled < n)
    {
        uint64_t nsamples;

        xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);

        // Render up to the next callback, so it runs at the right sample.

        if (opl_paused || OPL_Queue_IsEmpty(callback_queue))
        {
            nsamples = n - filled;
        }
        else
        {
            uint64_t next = OPL_Queue_Peek(callback_queue) + pause_offset;

            nsamples = (next - current_time) * OPL_ESP_RATE;
            nsamples = (nsamples + OPL_SECOND - 1) / OPL_SECOND;

            if (nsamples > n - filled)
            {
                nsamples = n - filled;
            }
        }

        xSemaphoreGive(callback_queue_mutex);

        if (nsamples > MAX_BLOCK)
        {
            nsamples = MAX_BLOCK;
        }

        if (nsamples > 0)
        {
            xSemaphoreTake(chip_mutex, portMAX_DELAY);
            Chip__GenerateBlock2(&opl_chip, nsamples, mix_buffer);
            xSemaphoreGive(chip_mutex);

            for (unsigned int i = 0; i < nsamples; ++i)
            {
                out[filled + i] += mix_buffer[i];
            }
        }

        filled += nsamples;
        AdvanceTime(nsamples);
    }
}

static void OPL_ESP_Shutdown(void)
{
    initialized = false;
}

static int OPL_ESP_Init(unsigned int port_base)
{
    opl_paused = 0;
    pause_offset = 0;
    current_time = 0;

    if (callback_queue == NULL)
    {
        callback_queue = OPL_Queue_Create();
        callback_mutex = xSemaphoreCreateMutex();
        callback_queue_mutex = xSemaphoreCreateMutex();
        chip_mutex = xSemaphoreCreateMutex();
    }
    else
    {
        OPL_Queue_Clear(callback_queue);
    }

    DBOPL_InitTables();
    Chip__Chip(&opl_chip);
    Chip__Setup(&opl_chip, OPL_ESP_RATE);

    initialized = true;
    return 1;
}

static unsigned int OPL_ESP_PortRead(opl_port_t port)
{
    unsigned int result = 0;

    if (port == OPL_REGISTER_PORT_OPL3)
    {
        return 0xff;
    }

    if (timer1.enabled && current_time > timer1.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x40;   // Timer 1 has expired
    }

    if (timer2.enabled && current_time > timer2.expire_time)
    {
        result |= 0x80;   // Either have expired
        result |= 0x20;   // Timer 2 has expired
    }

    return result;
}

static void OPLTimer_CalculateEndTime(opl_timer_t *timer)
{
    int tics;

    if (timer->enabled)
    {
        tics = 0x100 - timer->value;
        timer->expire_time = current_time
                           + ((uint64_t) tics * OPL_SECOND) / timer->rate;
    }
}

static void WriteRegister(unsigned int reg_num, unsigned int value)
{
    switch (reg_num)
    {
        case OPL_REG_TIMER1:
            timer1.value = value;
            OPLTimer_CalculateEndTime(&timer1);
            break;

        case OPL_REG_TIMER2:
            timer2.value = value;
            OPLTimer_CalculateEndTime(&timer2);
            break;

        case OPL_REG_TIMER_CTRL:
            if (value & 0x80)
            {
                timer1.enabled = 0;
                timer2.enabled = 0;
            }
            else
            {
                if ((value & 0x40) == 0)
                {
                    timer1.enabled = (value & 0x01) != 0;
                    OPLTimer_CalculateEndTime(&timer1);
                }

                if ((value & 0x20) == 0)
                {
                    timer2.enabled = (value & 0x02) != 0;
                    OPLTimer_CalculateEndTime(&timer2);
                }
            }
            break;

        default:
            xSemaphoreTake(chip_mutex, portMAX_DELAY);
            Chip__WriteReg(&opl_chip, reg_num, value);
            xSemaphoreGive(chip_mutex);
            break;
    }
}

static void OPL_ESP_PortWrite(opl_port_t port, unsigned int value)
{
    if (port == OPL_REGISTER_PORT)
    {
        register_num = value;
    }
    else if (port == OPL_REGISTER_PORT_OPL3)
    {
        register_num = value | 0x100;
    }
    else if (port == OPL_DATA_PORT)
    {
        WriteRegister(register_num, value);
    }
}

static void OPL_ESP_SetCallback(uint64_t us, opl_callback_t callback,
                                void *data)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_Push(callback_queue, callback, data,
                   current_time - pause_offset + us);
    xSemaphoreGive(callback_queue_mutex);
}

static void OPL_ESP_ClearCallbacks(void)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_Clear(callback_queue);
    xSemaphoreGive(callback_queue_mutex);
}

static void OPL_ESP_Lock(void)
{
    xSemaphoreTake(callback_mutex, portMAX_DELAY);
}

static void OPL_ESP_Unlock(void)
{
    xSemaphoreGive(callback_mutex);
}

static void OPL_ESP_SetPaused(int paused)
{
    opl_paused = paused;
}

static void OPL_ESP_AdjustCallbacks(float factor)
{
    xSemaphoreTake(callback_queue_mutex, portMAX_DELAY);
    OPL_Queue_AdjustCallbacks(callback_queue, current_time, factor);
    xSemaphoreGive(callback_queue_mutex);
}

opl_driver_t opl_esp_driver =
{
    "ESP32",
    OPL_ESP_Init,
    OPL_ESP_Shutdown,
    OPL_ESP_PortRead,
    OPL_ESP_PortWrite,
    OPL_ESP_SetCallback,
    OPL_ESP_ClearCallbacks,
    OPL_ESP_Lock,
    OPL_ESP_Unlock,
    OPL_ESP_SetPaused,
    OPL_ESP_AdjustCallbacks,
};
