# CyberPi hardware map

Source: Makeblock CyberPi-Library-for-Arduino (GPL-3.0), cloned in `ref/`, plus esptool readout.

## SoC
- ESP32-D0WD rev 1.1 (WROVER-B), 240 MHz dual core, 8 MB PSRAM (only 4 MB directly mappable), 8 MB flash (GigaDevice c8/4017)
- USB-serial: CH340 on /dev/ttyUSB0. 921600 baud drops bytes; use 460800.

## I2C bus (I2C_NUM_1, 400 kHz)
- SDA GPIO19, SCL GPIO18

| Addr | Device | Use |
|---|---|---|
| 0x58 | AW9523B #1 | GPIO expander: buttons, joystick, LCD DC/RST/backlight, amp enable |
| 0x5B | AW9523B #2 | LED-dimming mode: 5 RGB LEDs, regs 0x20.. (15 bytes R,G,B ×5) |
| 0x69 | MPU6887 | 6-axis gyro/accel |
| ?    | ES8218E | mic ADC (I2S_NUM_1 RX, MCLK on GPIO0 CLK_OUT1) |

## LCD: ST7735-class, 128×128, RGB565
- SPI host 1 (HSPI), 20 MHz in Makeblock code, DMA channel 1
- MOSI GPIO2, CLK GPIO4, CS GPIO12, MISO GPIO26 (only for the font ROM)
- DC = AW9523B P1_4, RST = P1_5, backlight = P1_7  ← DC goes over I2C
- Panel offset: x +3, y +2. Init MADCTL 0xA8, later 0xB8.
- Font ROM GT30L24A3W shares the bus, CS GPIO27 (not needed).

## Inputs (AW9523B 0x58, active low; aw_digitalRead inverts)
AW9523B reads do not auto-increment: read input registers 0x00 and 0x01 with
separate transactions (a 2-byte read returns garbage for port 1).

| Input | Pin |
|---|---|
| Joystick up / down / left / right / press | P0_1 / P0_4 / P0_0 / P0_2 / P0_3 |
| Button A / B | P0_6 / P0_5 |
| Menu (home) | P1_0 |
- Light sensor: ADC GPIO33

## Audio
- Speaker: built-in DAC right channel (GPIO25) via I2S_NUM_0 DAC mode, 16-bit, 20 kHz in Makeblock code
- Amp enable: AW9523B P1_3 high
- The amp is loud: one step of the 8-bit DAC is clearly audible. A constant
  128 is silent. The IDF `dac_continuous` driver works up to at least
  352.8 kHz (APLL), but each DMA descriptor must stay under ~4 KB, and each
  8-bit sample takes 16 bits of DMA.

## Stock flash layout
See backup/README.md.
