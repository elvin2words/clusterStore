# ClusterStore Firmware Workspace

This workspace is the production-facing firmware scaffold for the native ClusterStore node path while keeping the portable control core testable on a host machine.

## Node Deployment Modes

ClusterStore supports two node deployment modes that converge at the Cluster EMS layer:

- `Mode A: Native node`
  STM32G474RET6 node hardware running the portable firmware core through a board-support package.
- `Mode B: Overlay node`
  Existing BESS assets such as Victron, Pylontech, Growatt, or Deye units wrapped through a `bms_adapter` over Modbus or CAN BMS protocols.

Only the native node path runs this firmware workspace directly. Overlay nodes still converge with the same Cluster EMS semantics above the adapter layer.

## Current Decisions Locked In

- MCU: `STM32G474RET6`
- CAN peripheral: `FDCAN1` at `500 kbps` classic CAN for MVP
- Watchdog: `IWDG`
- NVM: internal flash with reserved metadata pages
- OTA: dual-slot bootloader with CRC32 now and signature-ready image header scaffolding

## Memory Map

```text
0x08000000  32 KB  Bootloader
0x08008000   4 KB  BCB-A
0x08009000   4 KB  BCB-B
0x0800A000  24 KB  Event journal
0x08010000 192 KB  Slot A
0x08040000 192 KB  Slot B
0x08070000  64 KB  Reserved
```

## Layout

```text
clusterstore-firmware/
|-- CMakeLists.txt
|-- VERSION
|-- cmake/
|-- lib/
|   |-- boot_control/
|   |-- cluster_platform/
|   `-- journal/
`-- tests/
    `-- fixtures/
```

## What Exists Today

- `lib/boot_control/`: portable boot control block logic, CRC32, dual-copy BCB persistence, and signature-ready image header definitions.
- `lib/journal/`: double-buffered journal metadata and fixed-record flash journal handling.
- `lib/cluster_platform/`: portable vtable seam for flash, CAN, time, and logging.
- `tests/`: host-side unit tests plus a 2 KB granularity flash simulator.
- `app/`: a CAN-only bench image, a native node runtime wrapper around the controller layer, linker scripts, and shared board defaults.
- `boot/`: a real bootloader image entry point wired to the dual-slot boot runtime.

## BSP Bring-Up Surface

The STM32G474 BSP lives under `bsp/stm32g474/` and is the next layer above the portable libraries. It now has dedicated modules for:

- `cs_can_g474.*` for `FDCAN1` classic-CAN bring-up and RX ring buffering
- `cs_flash_g474.*` for internal flash page erase/program/read helpers
- `cs_ina228.*` and `cs_adc_g474.*` for current, voltage, and temperature sampling
- `cs_iwdg_g474.*` for watchdog start and refresh
- `cs_bsp_g474.*` to bind those modules into the portable `cs_platform_t` seam
- `cs_cluster_bridge_g474.*` to bind the STM32 BSP into the existing node-controller runtime
- `cube_generated/` for the Cube-style board init, MSP, interrupt, and HAL config tree

These BSP files compile as HAL-backed code only when `CS_G474_USE_HAL=ON`. Otherwise they stay as portable stubs so the rest of the workspace remains host-friendly.

## Native Build Targets

- `cs_can_bench_g474`: first CAN-only bench image for the `NUCLEO-G474RE`
- `cs_native_node_g474_slot_a` / `cs_native_node_g474_slot_b`: native node images linked for the agreed slot map
- `cs_bootloader_g474`: bootloader image linked at `0x08000000`

These are exposed through CMake options:

- `CS_BUILD_CAN_BENCH_APP`
- `CS_BUILD_NATIVE_NODE_APP`
- `CS_BUILD_BOOTLOADER`
- `CS_G474_USE_HAL`

## Current Validation Status

- `scripts/check-firmware-env.ps1` is green on Windows with the `C:\tools` ARM and MinGW toolchains.
- The portable C layer and no-HAL BSP path host-build and pass `4/4` CTest cases under `build/host-mingw`.
- `npm run test:firmware-binding`, `npm run test:overlay-adapter`, `npm run check`, and `npm test` all pass in this workspace.

## Known Gaps

- The portable `lib/boot_control`, `lib/journal`, and `lib/cluster_platform` modules are now validated on host, but the native STM32 image targets still bridge through the older `firmware/node-firmware` runtime stack rather than consuming those new modules directly.
- The HAL-backed ARM build is currently blocked only by the missing STM32Cube G4 `Drivers/` tree under `bsp/stm32g474/cube_generated/Drivers/`.
- The FDCAN IRQ callback path is now wired into the application entry points, but board defaults still use polling until the first hardware bring-up proves the interrupt-driven path.
- There is still no HIL execution or real NUCLEO bring-up in this repository state.

## What Waits For Hardware

- Real STM32Cube HAL/CMSIS driver pack under `bsp/stm32g474/cube_generated/Drivers/`
- HAL-backed target compilation and link with `arm-none-eabi-gcc`
- CAN-only bench execution on a real `NUCLEO-G474RE` plus transceiver and USB-CAN adapter
- Convergence of the native STM32 images onto the new portable firmware core so host-tested code is the same code that runs on target

## Hardware-Free Validation

Use `scripts/check-firmware-env.ps1` as the first validation pass on Windows. It detects whether the ARM toolchain, `cmake`, a host compiler, `mingw32-make` or `ninja`, and the STM32Cube HAL driver tree are actually available, then runs an ARM syntax smoke build across the portable core, the no-HAL BSP path, and the host-side test sources.

```powershell
.\scripts\check-firmware-env.ps1
```

If PowerShell blocks local scripts on your machine, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\check-firmware-env.ps1
```

If `cmake`, a host compiler, and either `mingw32-make` or `ninja` are present, the same script also attempts the host configure/build for `CS_G474_USE_HAL=OFF`.
