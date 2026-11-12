# CyberDoom

doomgeneric on the Makeblock CyberPi (ESP32-WROVER-B, 128×128 ST7735), with
network deathmatch for up to four CyberPis.

## Controls

A is the square button, B the triangle.

| Button | In game | In menus |
|---|---|---|
| Joystick | move / turn | navigate |
| A (square) | fire | select |
| B (triangle) | use (doors, switches) | back |
| B + left/right | strafe | |
| B + up/down | next / previous weapon | |
| Joystick press | automap | select; "yes" on prompts |
| Menu (home) | open menu | close menu |

Always-run is on by default (`joyb_speed 29` in default.cfg). Quit Game
returns to the start screen without asking.

## Start screen

The CyberPi boots into a start screen: Single Player, Host Game, Join Game,
and your player name (shown to the other players). Names are edited with the
joystick (up/down picks a letter, left/right moves) and kept in
`/data/netplay.txt`.

## Network games

- **Host Game**: name the game; the CyberPi becomes a Wi-Fi access point
  called `DOOM <name>` (WPA2, password `cyberdoom`, channel 6) for up to
  three other players.
- **Join Game**: lists the games in range; pick one to join.
- **Lobby**: shows who has joined, in their Doom colours. The host picks the
  mode (deathmatch, altdeath, co-op), map (E1M1–E1M9), skill, monsters and
  time limit, and starts with A. Menu leaves.
- In deathmatch the HUD shows your frags in place of the keys.
- Leaving a game, or losing the connection, returns to the start screen
  (with the reason, if it was an error); the others play on.

This is Chocolate Doom 2.2.1's netcode (`net_*.c`, unmodified apart from
the config binding) over UDP on lwIP (`main/net_udp.c`), with the host's
server reached at the access point's address. Doom's netcode is lockstep,
so everyone runs at the pace of the slowest device; expect about 18–30 fps.
Wi-Fi only starts for network games.

## Display

Doom renders 320×232 instead of 320×200, always in low detail (160
distinct columns; the Graphic Detail option is fixed), since the panel has
only 128. Every other column is copied out and scaled 160→128 (5:4). In game
that gives 128×112,
which keeps Doom's 4:3 pixel aspect and horizontal FOV and adds vertical view,
with a native 16-row HUD below (health, armor, ammo, keys). Title,
intermission and finale screens are drawn for 320×200 and stay letterboxed
at 128×96. The screen size defaults to full screen (Options → Screen Size,
all the way up); smaller sizes also show Doom's own status bar.

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

With several CyberPis plugged in, their `by-id` names collide (the CH340 has
no serial number): use `/dev/serial/by-path/...`, which names the USB socket.

### Remote control (testing)

Over the serial port, single characters press buttons, so the game can be
driven without touching the board: `u d l r c` joystick up/down/left/right/
press, `a` / `b` the A / B buttons, `m` menu (capitals hold for a second).
`S` prints `SHOT ` and a base64 dump of the 128×128 big-endian RGB565 frame.

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
- `FEATURE_MULTIPLAYER`: network games (compiled out upstream). The
  `ORIGCODE` parts of `D_StartNetGame` are enabled with it, and the
  single-player stubs in `dummy.c` are dropped.
- `DG_NET_LOBBY`: the platform supplies the player name and the controller's
  game settings (`DG_NetPlayerName`, `DG_NetGameSettings` in `d_net.c`), and
  `NET_WaitForLaunch` (`main/lobby.c`, instead of the text-mode `net_gui.c`).
- `DG_MAXNETNODES`: size of the server's client table (6 instead of 16, which
  took 400 KB).
- `DG_PLATFORM_EXIT`: `I_Quit` and `I_Error` call `DG_Quit` / `DG_Error`
  (restart into the start screen) instead of returning or calling `exit()`,
  and the ENDOOM text screen is skipped.
- `DG_QUIT_NO_PROMPT`: Quit Game quits without the yes/no prompt.
- `DG_LOW_DETAIL_ONLY`: always render in low detail.

Changes to the Chocolate Doom music code (not behind defines, since it isn't
upstream doomgeneric): `i_oplmusic.c` locks the OPL in all entry points that
touch voices (upstream only in StopSong; callbacks run on the other core here)
and loads songs from memory; `midifile.c` gains `MIDI_LoadMemory`; `opl.c`
uses FreeRTOS instead of SDL and serializes register writes.

The Chocolate Doom 2.2.1 net code (`net_*.c`, `aes_prng.c`) is unmodified
except: `net_client.c` binds `player_name` with doomgeneric's
`M_BindVariable`, `aes_prng.c` defines `SDL_SwapLE32` as a no-op, and
`net_defs.h` takes `DG_MAXNETNODES`.
