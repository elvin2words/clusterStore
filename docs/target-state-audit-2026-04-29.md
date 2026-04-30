# Target-State Audit

Date: `2026-04-29` _(last updated 2026-04-29 — reflects all committed work as of this date)_

This audit compares the current repo against what ClusterStore is supposed to be: a controller-ready clustered BESS platform with a runnable EMS, a runnable UtilityCore bridge, a reproducible native STM32 firmware path, and a practical deployment path into real CAN, Modbus, MQTT, LTE, and cloud environments.

## Audit Command Baseline

Verified on this workstation:

- `cmd /c npm run audit:full` -> pass
- `cmd /c npm run mqtt:mosquitto:check` -> fail, because `mosquitto.exe` is not installed or pointed to yet

Host observations:

- `choco.exe` exists on this machine
- `winget.exe` is not on PATH here
- `choco search mosquitto` did not surface a direct Mosquitto package in this environment

## What The System Is Supposed To Be

The intended target system is:

1. Native STM32 or overlay BESS nodes exposing normalized ClusterStore semantics
2. A controller host running EMS and the UtilityCore bridge as actual daemons
3. Real inverter integration over Modbus
4. Real telemetry and command integration over MQTT
5. Local HMI, watchdog, and field diagnostics seams
6. A reproducible build, smoke, and commissioning workflow

## Current State Against That Target

### 1. EMS Controller Runtime

Status: `meets local target`

Evidence:

- EMS has a real daemon entrypoint and runtime composition
- health, snapshot, and diagnostics endpoints are covered
- startup equalization and safe startup gating are tested
- live config validation confirms real adapter entrypoints exist

Remaining gap:

- real CAN data source and real inverter path still need site hardware

### 2. UtilityCore Bridge Runtime

Status: `meets local target`

Evidence:

- bridge has a real daemon entrypoint and runtime composition
- telemetry publishing, command flow, buffering, and replay are tested
- local smoke stack proves MQTT publish through the real daemon path
- secure config supports environment-backed secrets and TLS fields

Remaining gap:

- production broker CA file and production password are still absent, which is why live-readiness still warns

### 3. Native STM32 Firmware Path

Status: `meets local build target, not yet field-proven`

Evidence:

- the STM32Cube G4 driver tree is now present
- host firmware checks pass
- ARM HAL build passes and produces:
  - `cs_can_bench_g474`
  - `cs_native_node_g474_slot_a`
  - `cs_native_node_g474_slot_b`
  - `cs_bootloader_g474`

Remaining gap:

- board flash, boot, rollback, watchdog, and live CAN validation still require hardware

### 4. Overlay Node Commercial Path

Status: `partial`

Evidence:

- the normalized EMS-side `bms_adapter` seam exists
- overlay adapter tests pass

Remaining gap:

- vendor-specific adapters for real Victron, Pylontech, Growatt, or Deye assets are still the next commercial development step

### 5. Local MQTT Development Path

Status: `repo-ready, host-not-ready`

Evidence:

- the repo now contains:
  - `npm run mqtt:mosquitto:check`
  - `npm run mqtt:mosquitto:run`
  - a local Mosquitto config
  - a launcher script that resolves `mosquitto.exe`

Remaining gap:

- no real Mosquitto binary is installed on this workstation yet

### 6. Deployment and Commissioning Guidance

Status: `meets documentation target`

Evidence:

- the repo now has:
  - a deployment guide
  - an operations runbook
  - a full implementation walkthrough
  - a next-work roadmap
  - a git packaging plan

Remaining gap:

- the guides still depend on the real site values, credentials, and hardware endpoints that cannot be fabricated from the repo

## STM32Cube Decision

Keep STM32Cube in the current baseline.

Reason:

1. the current ARM build depends on the imported driver tree and generated startup/system files
2. the BSP still relies on Cube-generated peripheral and clock surfaces
3. removing Cube now would reduce confidence before HIL and hardware bring-up are complete

Revisit this only after:

- first-board bring-up
- CAN bench validation
- bootloader and slot validation on real STM32 hardware

## Remaining Gaps That Still Need Human Or Site Intervention

These are now the true blockers, not missing repo code:

1. install or provide `mosquitto.exe` if you want a real local broker on this host
2. place the real MQTT CA certificate
3. provide the real MQTT credentials
4. connect the CAN adapter to real site data
5. connect the watchdog adapter to the real local supervisor
6. prove the real Modbus inverter register map
7. validate LTE modem outage and replay against the real modem path
8. flash and validate the STM32 images on target hardware

## Audit Verdict

The repo is now locally operational and reproducible, and it matches the intended software/system architecture far more closely than it did at the start of the audit cycle.

The remaining blockers are no longer "missing implementation" blockers. They are now mostly:

- real credentials
- real certificates
- real host software installation
- real hardware commissioning

---

## Changes Since Initial Audit

The following items were completed after this audit was first written and are now reflected in the committed baseline:

### Completed

1. **node-firmware new modules committed** — `cluster_bootloader_runtime`, `cluster_node_runtime`, `cluster_persistent_state`, `cluster_stm32_boot`, `cluster_stm32_hal` added
2. **clusterstore-firmware BSP fully wired** — STM32Cube G4 Drivers tree imported, `cs_syscalls.c` bare-metal stubs added, `build-g474-hal.ps1` script committed, CMakeLists updated across all targets
3. **ARM toolchain fixed** — `arm-none-eabi.cmake` updated, clean-configure now default behavior
4. **HAL config and generated files updated** — `stm32g4xx_hal_conf.h`, `cs_cube_g474_board.c`, `system_stm32g4xx.c` aligned to working ARM build
5. **Full documentation set committed** — all docs/ files staged and pushed including this audit, architecture, master-plan, deployment guide, operations runbook, roadmap, and implementation walkthrough
6. **Contracts package.json and tsconfig updated** — workspace dependency and TypeScript path alignment
7. **`clusterStoreDev.md` committed** — product description and layer-by-layer reference

### Still Open From This Audit

1. `mosquitto.exe` not installed on this host
2. Real MQTT CA certificate and credentials absent
3. CAN adapter not connected to real node hardware
4. Modbus inverter register map not proven
5. LTE modem outage and replay not exercised on real modem
6. STM32 images not yet flashed or validated on hardware
7. Firmware runtime convergence (portable `lib/` ↔ STM32 image entry points) still incomplete
