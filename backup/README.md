# CyberPi flash backup (2026-10-21)

Stock Makeblock firmware 44.01.016 (build Nov 14 2025).
Chip: ESP32-D0WD rev 1.1, MAC 34:5f:45:de:fd:1c, 8MB GigaDevice flash (c8/4017).

Two full dumps; they differ only in the NVS partition (0xb000-0xcfff), which the
firmware writes on every boot. All code/data partitions are identical.

Partitions: nvs 0x9000, otadata 0xd000, phy_init 0xf000, factory 0x10000 (960K),
ota_0 0x100000 (3.5M), storage 0x480000 (3M), unused 0x780000-0x800000.

Restore (CH340 is unreliable at 921600, use 460800):

    ../.venv/bin/esptool --port /dev/ttyUSB0 --baud 460800 write-flash 0 cyberpi-flash-8MB.bin
