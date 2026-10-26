#include "board.h"

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define I2C_SDA 19
#define I2C_SCL 18
#define AW_IO_ADDR  0x58 // GPIO expander: buttons, LCD control
#define AW_LED_ADDR 0x5B // LED driver: RGB LEDs

#define AW_REG_IN0   0x00
#define AW_REG_OUT0  0x02
#define AW_REG_OUT1  0x03
#define AW_REG_CFG0  0x04
#define AW_REG_CFG1  0x05
#define AW_REG_CTRL  0x11
#define AW_REG_MODE0 0x12
#define AW_REG_MODE1 0x13
#define AW_REG_DIM0  0x20
#define AW_REG_RST   0x7F

// Expander port 1 bits
#define P1_AMP_EN (1u << 3)
#define P1_LCD_DC (1u << 4)
#define P1_LCD_RST (1u << 5)
#define P1_LCD_BL (1u << 7)

#define LCD_HOST SPI2_HOST // "HSPI" / SPI_HOST 1 in IDF 3.x naming
#define LCD_MOSI 2
#define LCD_CLK  4
#define LCD_CS   12
#define LCD_XOFF 3
#define LCD_YOFF 2

#define LCD_CHUNK 8192 // bytes per DMA transaction

static i2c_master_dev_handle_t s_aw_io, s_aw_led;
static spi_device_handle_t s_lcd;
static uint8_t s_p1_out;
static int s_dc = -1;

static void aw_write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    ESP_ERROR_CHECK(i2c_master_transmit(dev, buf, 2, 100));
}

static void set_p1(uint8_t mask, bool on)
{
    s_p1_out = on ? (s_p1_out | mask) : (s_p1_out & ~mask);
    aw_write(s_aw_io, AW_REG_OUT1, s_p1_out);
}

// DC lives on the I2C expander, so only touch it when the level changes.
static void lcd_dc(int level)
{
    if (s_dc != level) {
        set_p1(P1_LCD_DC, level);
        s_dc = level;
    }
}

static void lcd_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    spi_transaction_t t = {.length = 8, .tx_buffer = &cmd};
    lcd_dc(0);
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd, &t));
    if (len) {
        spi_transaction_t d = {.length = len * 8, .tx_buffer = data};
        lcd_dc(1);
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_lcd, &d));
    }
}

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t delay_ms;
    uint8_t data[16];
} lcd_init_t;

// ST7735 init sequence from Makeblock's lcd.c
static const lcd_init_t s_lcd_init[] = {
    {0x01, 0, 120, {0}},                            // SWRESET
    {0x11, 0, 120, {0}},                            // SLPOUT
    {0xB1, 3, 0, {0x01, 0x2C, 0x2D}},               // FRMCTR1
    {0xB2, 3, 0, {0x01, 0x2C, 0x2D}},               // FRMCTR2
    {0xB3, 6, 0, {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}},
    {0xB4, 1, 0, {0x07}},                           // INVCTR
    {0xC0, 3, 0, {0xA2, 0x02, 0x84}},               // PWCTR1
    {0xC1, 1, 0, {0xC5}},
    {0xC2, 2, 0, {0x0A, 0x00}},
    {0xC3, 2, 0, {0x8A, 0x2A}},
    {0xC4, 2, 0, {0x8A, 0xEE}},
    {0xC5, 1, 0, {0x0E}},                           // VMCTR1
    {0x20, 0, 0, {0}},                              // INVOFF
    {0x36, 1, 0, {0xA8}},                           // MADCTL
    {0x3A, 1, 0, {0x05}},                           // COLMOD RGB565
    {0xE0, 16, 0, {0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
                   0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10}},
    {0xE1, 16, 0, {0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                   0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10}},
    {0x13, 0, 10, {0}},                             // NORON
    {0x29, 0, 100, {0}},                            // DISPON
};

static void expanders_init(void)
{
    // LED driver: all pins in LED (constant current) mode
    aw_write(s_aw_led, AW_REG_RST, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw_write(s_aw_led, AW_REG_MODE0, 0x00);
    aw_write(s_aw_led, AW_REG_MODE1, 0x80); // P1_7 (DIM15, no LED) as GPIO: menu button?
    aw_write(s_aw_led, AW_REG_CTRL, 0x00);
    aw_write(s_aw_led, AW_REG_CFG1, 0x80);

    // IO expander: P0 = inputs (buttons), P1 = outputs except P1_0 (menu)
    aw_write(s_aw_io, AW_REG_RST, 0x00);
    vTaskDelay(pdMS_TO_TICKS(10));
    aw_write(s_aw_io, AW_REG_MODE0, 0xFF);
    aw_write(s_aw_io, AW_REG_MODE1, 0xF9); // P1_1, P1_2 in LED mode, as Makeblock does
    aw_write(s_aw_io, AW_REG_CTRL, 0x00);
    s_p1_out = 0;
    aw_write(s_aw_io, AW_REG_OUT1, s_p1_out);
    aw_write(s_aw_io, AW_REG_CFG0, 0xFF);
    aw_write(s_aw_io, AW_REG_CFG1, 0x01);
}

static void lcd_init(uint32_t hz)
{
    spi_bus_config_t bus = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_CHUNK,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = hz,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 4,
        // Write-only; half duplex lifts the 26.7 MHz GPIO-matrix full-duplex limit
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_HOST, &dev, &s_lcd));

    set_p1(P1_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    set_p1(P1_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    for (size_t i = 0; i < sizeof(s_lcd_init) / sizeof(s_lcd_init[0]); i++) {
        lcd_cmd(s_lcd_init[i].cmd, s_lcd_init[i].data, s_lcd_init[i].len);
        if (s_lcd_init[i].delay_ms)
            vTaskDelay(pdMS_TO_TICKS(s_lcd_init[i].delay_ms));
    }

    // The window never changes, so set it once.
    uint8_t caset[4] = {0, LCD_XOFF, 0, LCD_XOFF + LCD_W - 1};
    uint8_t raset[4] = {0, LCD_YOFF, 0, LCD_YOFF + LCD_H - 1};
    lcd_cmd(0x2A, caset, 4);
    lcd_cmd(0x2B, raset, 4);

    set_p1(P1_LCD_BL, 1);
}

void board_init(uint32_t lcd_hz)
{
    i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_1,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t h;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &h));
    for (uint8_t a = 0x08; a < 0x78; a++)
        if (i2c_master_probe(h, a, 20) == ESP_OK)
            ESP_LOGI(TAG, "i2c device at 0x%02x", a);

    i2c_device_config_t dc = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .scl_speed_hz = 400000};
    dc.device_address = AW_IO_ADDR;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(h, &dc, &s_aw_io));
    dc.device_address = AW_LED_ADDR;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(h, &dc, &s_aw_led));

    expanders_init();
    lcd_init(lcd_hz);
}

void board_lcd_push(const uint16_t *fb)
{
    static spi_transaction_t t[(LCD_W * LCD_H * 2 + LCD_CHUNK - 1) / LCD_CHUNK];
    const uint8_t *p = (const uint8_t *)fb;
    int n = 0;

    lcd_cmd(0x2C, NULL, 0); // RAMWR
    lcd_dc(1);
    for (int off = 0; off < LCD_W * LCD_H * 2; off += LCD_CHUNK, n++) {
        t[n] = (spi_transaction_t){.length = LCD_CHUNK * 8, .tx_buffer = p + off};
        ESP_ERROR_CHECK(spi_device_queue_trans(s_lcd, &t[n], portMAX_DELAY));
    }
    while (n--) {
        spi_transaction_t *done;
        ESP_ERROR_CHECK(spi_device_get_trans_result(s_lcd, &done, portMAX_DELAY));
    }
}

// The AW9523B doesn't auto-increment on reads, so read each port separately.
static uint16_t aw_read_inputs(i2c_master_dev_handle_t dev)
{
    uint8_t reg0 = AW_REG_IN0, reg1 = AW_REG_IN0 + 1, in0 = 0xFF, in1 = 0xFF;
    i2c_master_transmit_receive(dev, &reg0, 1, &in0, 1, 50);
    i2c_master_transmit_receive(dev, &reg1, 1, &in1, 1, 50);
    return in0 | (in1 << 8);
}

uint16_t board_expander_raw(void)
{
    return aw_read_inputs(s_aw_io);
}

uint16_t board_led_expander_raw(void)
{
    return aw_read_inputs(s_aw_led);
}

uint16_t board_buttons(void)
{
    return (uint16_t)~aw_read_inputs(s_aw_io) & 0x017F;
}

void board_leds(const uint8_t rgb[15])
{
    uint8_t buf[16] = {AW_REG_DIM0};
    for (int i = 0; i < 15; i++)
        buf[i + 1] = rgb[i];
    i2c_master_transmit(s_aw_led, buf, sizeof(buf), 50);
}
