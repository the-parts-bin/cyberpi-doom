# Backing up and restoring the stock firmware

Flashing Doom overwrites the whole 8 MB flash, including Makeblock's firmware.
Make a full flash dump first, so the CyberPi can be put back exactly as it was.

The dumps themselves (`*.bin`) are not in git: they're 8 MB each and contain
per-device data (MAC, calibration, NVS). Only the checksums of mine are
tracked, in `SHA256SUMS`, as a reference.

## Tools

Any esptool works: the one bundled with ESP-IDF (`esptool.py`, after
`. ~/esp/esp-idf-v5.5.6/export.sh`), or a standalone one:

```sh
python3 -m venv .venv && .venv/bin/pip install esptool   # gives .venv/bin/esptool
```

The commands below use `esptool.py`; with a standalone esptool v5 use
`esptool` instead (same arguments).

Use the CyberPi's serial port by its stable name (e.g.
`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`, the CH340 adapter), not
`/dev/ttyUSBn`, which changes when other USB-serial devices are plugged in.
Use 460800 baud: the CH340 corrupts data at 921600.

```sh
PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

## Make a backup

Read the whole flash twice and compare, to catch transfer errors:

```sh
esptool.py --port $PORT --baud 460800 read_flash 0 0x800000 cyberpi-flash-8MB.bin
esptool.py --port $PORT --baud 460800 read_flash 0 0x800000 cyberpi-flash-8MB.read2.bin
cmp -l cyberpi-flash-8MB.bin cyberpi-flash-8MB.read2.bin | awk '{printf "0x%x\n", $1-1}' | sort -u | head
sha256sum cyberpi-flash-*.bin > SHA256SUMS
```

The stock firmware boots between the two reads and writes its NVS partition
(0x9000–0xcfff) each time, so a few differences there are expected. Any
difference elsewhere means a bad read: read again.

Keep the dump somewhere safe outside this directory too.

## Restore

```sh
sha256sum -c SHA256SUMS --ignore-missing   # make sure the dump is intact
esptool.py --port $PORT --baud 460800 write_flash 0 cyberpi-flash-8MB.bin
```

This writes all 8 MB (a few minutes) and replaces everything, Doom and its
saves included. The CyberPi then boots the Makeblock firmware again and works
with mBlock as before.

## My dump (2026-10-21)

- Stock Makeblock firmware 44.01.016 (build Nov 14 2025)
- ESP32-D0WD rev 1.1, 8 MB GigaDevice flash (c8/4017)
- The two reads differ only in NVS (0xb000–0xcfff); all code and data
  partitions are identical.
- Stock partitions: nvs 0x9000, otadata 0xd000, phy_init 0xf000,
  factory 0x10000 (960 KB), ota_0 0x100000 (3.5 MB), storage 0x480000 (3 MB),
  unused 0x780000–0x800000.
