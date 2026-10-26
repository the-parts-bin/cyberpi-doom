# CyberDoom

doomgeneric on the Makeblock CyberPi (ESP32-WROVER-B, 128×128 ST7735).

## Controls

| Button | In game | In menus |
|---|---|---|
| Joystick | move / turn | navigate |
| A | fire | select |
| B | use (doors, switches) | back |
| B + left/right | strafe | |
| B + up/down | next / previous weapon | |
| Joystick press | automap | select; "yes" on prompts |
| Menu (home) | open menu | close menu |

Always-run is on by default (`joyb_speed 29` in default.cfg).

## Display

Doom renders 320×232 instead of 320×200. In game that is scaled to 128×112,
which keeps Doom's 4:3 pixel aspect and horizontal FOV and adds vertical view,
with a native 16-row HUD below (health, armor, ammo, keys). Title,
intermission and finale screens are drawn for 320×200 and stay letterboxed
at 128×96. Use the full-screen size (Options → Screen Size, all the way up)
to hide Doom's own status bar.

## Build and flash

```sh
. ~/esp/esp-idf-v5.5.6/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 -b 460800 flash        # bootloader + partitions + app
idf.py -p /dev/ttyUSB0 -b 460800 app-flash    # app only, day-to-day

# once, or after erasing the chip:
esptool.py --port /dev/ttyUSB0 --baud 460800 write_flash 0x200000 ../wad/doom1.wad
```

Use 460800 baud; the CH340 corrupts data at 921600.

`doom1.wad` is not in git. Use the shareware v1.9 IWAD (4,196,020 bytes,
MD5 `f0cefca49926d00903cf57551d901abe`), e.g. from RPM Fusion's
`doom-shareware` package, and put it in `wad/`.

## Layout

| Partition | Offset | Size | Contents |
|---|---|---|---|
| factory | 0x10000 | 1.9 MB | app |
| wad | 0x200000 | 4.1 MB | raw doom1.wad, served read-only at /wad/doom1.wad |
| storage | 0x610000 | 1.9 MB | FAT at /data: default.cfg, savegame/ |

## Changes to doomgeneric

All behind defines set in `components/doomgeneric/CMakeLists.txt`:
- `DG_DIRECT_VIDEOBUFFER`: skip the copy into `DG_ScreenBuffer`; the platform reads `I_VideoBuffer`.
- `DG_CONFIG_DIR`: config and saves under `/data/` instead of `.`.
- `DG_AUTO_SAVE_NAMES`: saving names the game after its slot ("SLOT n") instead of asking for text.
- `DG_SCREENHEIGHT`: screen height override (232). The status bar, automap title,
  view sizes, sky and weapon sprite positions were made relative to
  `SCREENHEIGHT`/`ST_Y`, so they're unchanged at 200.
- `DG_NO_AUTOMAP_STATUSBAR`: no Doom status bar on the full-screen automap; the map uses the full height.
- `linker.lf`: Doom's .bss goes to PSRAM.
