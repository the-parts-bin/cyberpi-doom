# CyberPi Doom

Doom running on the [Makeblock CyberPi](https://www.makeblock.com/),
an ESP32 learning board with a 128×128 colour screen, a joystick, buttons and
a speaker.

- Shareware Doom, based on [doomgeneric](https://github.com/ozkl/doomgeneric):
  the full game at about 30 fps, scaled down to the 128×128 screen.
- A taller view than stock Doom (320×232), with the same field of view and a
  compact HUD below it.
- Sound effects, plus OPL music emulated in software on the second core.
- Joystick and button controls, saved games and saved settings.

## What's here

| Path | Contents |
|---|---|
| [doom/](doom/) | The ESP-IDF project for Doom. Its [README](doom/README.md) has the controls, how to build and flash, and the changes made to doomgeneric. |
| [HARDWARE.md](HARDWARE.md) | CyberPi pin map and hardware quirks, taken from Makeblock's [Arduino library](https://github.com/Makeblock-official/CyberPi-Library-for-Arduino). |
| [hwtest/](hwtest/) | A small bring-up firmware that tests the screen, buttons and LEDs. |
| [backup/](backup/) | How to back up and restore the stock Makeblock firmware. |
| `chip-info.txt` | esptool's readout of the board. |

## Getting started

1. **Back up the stock firmware** first ([backup/README.md](backup/README.md)).
   Flashing Doom replaces it.
2. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.5.
3. Get the shareware `doom1.wad` (v1.9) and put it in `wad/`.
4. Build and flash, following [doom/README.md](doom/README.md).

## Hardware

- ESP32-WROVER-B: dual-core 240 MHz, 8 MB flash, 8 MB PSRAM (4 MB usable).
- ST7735 128×128 LCD over SPI.
- Buttons, joystick and LEDs on AW9523B I²C expanders.
- Speaker on the ESP32's 8-bit DAC.
- Tested with the Pocket Shield (battery).

## Licence

Doom, doomgeneric and the Chocolate Doom music code are GPL-2.0; see the
`LICENSE` files in [doom/components/](doom/components/). The rest of the code
here is under the same terms. The Doom WAD is not included.
