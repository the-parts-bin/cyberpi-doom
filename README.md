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

## Install the prebuilt image

No toolchain needed. You need the CyberPi on USB, Python (for esptool) and
the shareware `doom1.wad` v1.9 (4,196,020 bytes, MD5
`f0cefca49926d00903cf57551d901abe`; it's freely distributable but not
included here, e.g. from RPM Fusion's `doom-shareware` package).

1. **Back up the stock firmware** first ([backup/README.md](backup/README.md)).
   Flashing Doom replaces it, and the backup is the only way back.
2. Download `cyberdoom-<version>.bin` from the
   [Releases](https://github.com/the-parts-bin/cyberpi-doom/releases) page.
3. Flash the image at 0 and the WAD at 0x200000:

   ```sh
   pip install esptool
   esptool --port /dev/ttyUSB0 --baud 460800 write-flash \
       0x0 cyberdoom-v1.0.bin 0x200000 doom1.wad
   ```

   Use your CyberPi's port (`/dev/ttyUSB*` on Linux,
   `/dev/cu.wchusbserial*` on macOS, `COMx` on Windows). Keep the baud rate
   at 460800 or lower: the CyberPi's USB-serial chip (CH340) corrupts data
   at 921600. Older esptool versions are called `esptool.py` and spell the
   command `write_flash`.

   Without Python, Espressif's browser flasher
   (<https://espressif.github.io/esptool-js/>, Chrome or Edge) does the same:
   connect at 460800, add the image at `0x0` and `doom1.wad` at `0x200000`,
   then Program.

4. esptool restarts the CyberPi when it's done (with the browser flasher,
   unplug and replug it). It boots into the start screen; see [doom/README.md](doom/README.md) for controls and
   network games. Every CyberPi in a network game needs the same version.

To update to a newer image later, flash just the image at 0x0; the WAD and
your saves and settings stay.

## Build from source

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
