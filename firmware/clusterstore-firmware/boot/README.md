# Bootloader Image

This directory now contains the STM32G474 bootloader image entry point and linker script for the agreed 32 KB boot region.

Implemented contents:

- [main.c](main.c): bootloader entry point that restores persistent state, selects a slot, verifies CRC32, and jumps
- `linker/g474_bootloader.ld`: linker script for `0x08000000`

The runtime is intentionally transport-free: OTA download still happens in the application, while the bootloader only validates slot state and jumps or stays resident.

Current limitation:

- `cs_bootloader_g474` still uses the older `firmware/node-firmware` boot-control and persistent-state implementation. The newer portable `lib/boot_control` module is host-validated but not yet the bootloader path that runs on STM32.
