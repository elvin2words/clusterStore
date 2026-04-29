# Current Audit

Date: `2026-04-26`

## Scope

This audit covers the current uncommitted firmware and EMS work across:

- `firmware/clusterstore-firmware`
- `firmware/node-firmware`
- `services/cluster-ems`
- repo-level scripts, tests, and documentation that describe the current runtime state

## What Is Verified Right Now

- `npm run check` passes
- `npm test` passes
- `npm run test:firmware-binding` passes
- `npm run test:overlay-adapter` passes
- `firmware/clusterstore-firmware/scripts/check-firmware-env.ps1` is green on Windows with the `C:\tools` toolchains
- `firmware/clusterstore-firmware` host-builds and passes `4/4` CTest cases under `build/host-mingw`
- the ARM/CMake HAL configure step succeeds for the STM32 targets

## Key Findings

### 1. Portable Core and Native STM32 Runtime Are Still Split

The new portable firmware modules under `firmware/clusterstore-firmware/lib/` are real and host-validated, but the native STM32 app and bootloader targets still link through the older `firmware/node-firmware` runtime stack. That means the code currently proven on host is not yet the same code that will run on target.

### 2. HAL-Backed ARM Builds Stop at Missing STM32Cube Drivers

The first ARM build now gets through CMake configure and reaches source compilation. The first hard stop is the missing STM32 HAL header tree under `bsp/stm32g474/cube_generated/Drivers/`. There is not a deeper target-build failure in front of that missing dependency at the moment.

### 3. FDCAN IRQ Path Needed Real Wiring

The Cube-style board tree exposed an IRQ callback surface, but the application entry points did not override it and the MSP code did not enable the NVIC line. The repo now wires the callback into the app entry points and enables `FDCAN1_IT0_IRQn`, while still leaving default board configuration in polling mode until hardware bring-up proves the interrupt path.

### 4. Overlay Adapter Identifier Resolution Was Too Permissive

The overlay EMS adapter could previously resolve an asset when either `nodeId` or `nodeAddress` matched, even if the pair conflicted. That is now fixed so commands require all provided identifiers to resolve to the same overlay asset.

## Ordered Way Forward

1. Import the real STM32Cube G4 `Drivers/` tree into `firmware/clusterstore-firmware/bsp/stm32g474/cube_generated/Drivers/`
2. Build `cs_can_bench_g474`, `cs_bootloader_g474`, and `cs_native_node_g474_slot_a` with the ARM toolchain
3. Decide whether to migrate the older `firmware/node-firmware` runtime into the new portable `lib/` modules or retire the duplicated portable modules in favor of a single implementation
4. Run the first CAN bench on a NUCLEO-G474RE plus USB-CAN
5. Add HIL coverage for contactor sequencing, supervision timeout fallback, and boot rollback
6. Extend the overlay path beyond the normalized adapter seam into vendor-specific adapters

## Practical Rule For The Repo

Until runtime convergence is complete, treat `firmware/clusterstore-firmware/lib/` as the validated portable reference implementation and treat the STM32 native image path as integration scaffolding that still needs to be aligned with it.
