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

## Sound

Sound effects and OPL music, mixed at 22.05 kHz on core 0 (about 12–15% of
it) and played through the ESP32's 8-bit DAC on GPIO25 into the speaker amp.

- Effects play straight from flash: the DS* lumps are contiguous in the WAD,
  so their 525 KB span is memory-mapped once.
- Music is Chocolate Doom 2.2.1's OPL player (GENMIDI instruments) on the
  DOSBox DBOPL emulator (`components/opl`), with an ESP32 driver. Songs are
  converted MUS→MIDI in memory instead of via a temp file.
- One DAC step is clearly audible on this speaker, so the DAC runs at 16× the
  mix rate (352.8 kHz) and a 2nd-order noise shaper pushes the 8-bit rounding
  noise above the audio band. Quiet music still has some hiss.
- Silent blocks are output as a flat mid level, and music at volume 0 is
  dropped, so a muted game is truly silent.
- Both volumes default to 0 on a fresh config.

Settings are saved to `/data/default.cfg` whenever the menu closes.

## Build and flash

```sh
. ~/esp/esp-idf-v5.5.6/export.sh
idf.py build
idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 -b 460800 flash        # bootloader + partitions + app
idf.py -p /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 -b 460800 app-flash    # app only, day-to-day

# once, or after erasing the chip:
esptool.py --port /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0 --baud 460800 write_flash 0x200000 ../wad/doom1.wad
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
- `FEATURE_SOUND`: enables doomgeneric's sound hooks (`DG_sound_module` in
  `main/audio.c`, `DG_music_module` from `i_oplmusic.c`); the SDL parts of
  `i_sound.c` are skipped on `ESP_PLATFORM`.
- `m_config.c`: config load/save were compiled out upstream (`#if ORIGCODE`);
  they're enabled when `DG_CONFIG_DIR` is set. Loading also keeps
  doomgeneric's own key codes (fire, use, strafe: 0xa0–0xa3, which have no
  scancode) instead of zeroing them, and a 0 binding falls back to the default.
- `linker.lf`: Doom's .bss goes to PSRAM.

Changes to the Chocolate Doom music code (not behind defines, since it isn't
upstream doomgeneric): `i_oplmusic.c` locks the OPL in all entry points that
touch voices (upstream only in StopSong; callbacks run on the other core here)
and loads songs from memory; `midifile.c` gains `MIDI_LoadMemory`; `opl.c`
uses FreeRTOS instead of SDL and serializes register writes.
