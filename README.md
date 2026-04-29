# ClusterStore Platform

ClusterStore is the software backbone for a clustered energy storage system made of PowerHive / BESS nodes, a cluster controller, and the UtilityCore fleet platform above it.

The main planning and audit reference lives in `docs/clusterstore-master-plan.md`.

This repository now carries the full verified development stack across three runtime layers:

1. `firmware/node-firmware` for the embedded node controller contract and cluster-aware state machine.
2. `firmware/clusterstore-firmware` for the portable native-node firmware workspace, boot-control logic, flash journal, and host-testable C build scaffold.
3. `services/cluster-ems` for the cluster controller orchestration loop running on DIN rail IPC hardware or equivalent controller hardware.
4. `services/utilitycore-bridge` for the MQTT/LTE and local SCADA bridge into UtilityCore.
5. `packages/contracts` for the shared CAN, telemetry, MQTT, and command contracts across the stack.

## What Is Included

- A capability map that turns the ClusterStore pillars into concrete build requirements.
- Shared data contracts for node telemetry, EMS control, inverter integration, and UtilityCore telemetry.
- EMS orchestration code for startup equalization, dispatch allocation, fault isolation, and HMI/watchdog integration.
- Starter node firmware interfaces for CAN messaging and cluster-aware state transitions.
- A portable firmware workspace for the native STM32G474 node path with boot-control, journal, and host-side test seams.
- STM32G474 image targets for CAN bench, native node runtime, and bootloader entry points together with a Cube-style board tree.
- A first EMS-side `bms_adapter` that normalizes overlay BESS assets into the same node semantics used for native nodes.
- A UtilityCore bridge with MQTT publishing, command intake, acknowledgements, local SCADA fanout, and local stack smoke coverage.

## Verified Commands

The following commands are verified on this Windows workspace:

- `cmd /c npm run audit:full`
- `cmd /c npm run check`
- `cmd /c npm test`
- `cmd /c npm run build`
- `cmd /c npm run mqtt:mosquitto:check`
- `cmd /c npm run test:firmware-binding`
- `cmd /c npm run test:overlay-adapter`
- `cmd /c npm run sim:smoke`
- `cmd /c npm run smoke:stack`
- `cmd /c npm run check:live-readiness`
- `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/check-firmware-env.ps1`
- `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/build-g474-hal.ps1`

## Quick Start

1. Install workspace dependencies with `cmd /c npm install`.
2. Run `cmd /c npm run audit:full`.
3. Start the local EMS daemon with `cmd /c npm run start:ems`.
4. Start the local bridge daemon with `cmd /c npm run start:bridge`.
5. Run `cmd /c npm run smoke:stack` to boot a fake MQTT broker plus both daemons and verify the end-to-end CLI path.

Example daemon configs live at:

- `services/cluster-ems/config/example.daemon.json`
- `services/cluster-ems/config/example.live.daemon.json`
- `services/cluster-ems/config/example.can-adapter.json`
- `services/cluster-ems/config/example.watchdog-adapter.json`
- `services/utilitycore-bridge/config/example.daemon.json`
- `services/utilitycore-bridge/config/example.secure.daemon.json`

To structure-check a live cutover config pair before hardware is online, run:

- `cmd /c npm run check:live-readiness`

To probe real endpoints once credentials and hardware paths are available, run:

- `node scripts/live-readiness-check.mjs --ems <ems-config> --bridge <bridge-config> --probe`

To point the bridge at a real local Mosquitto daemon instead of the fake smoke broker, use:

- `cmd /c npm run mqtt:mosquitto:check`
- `cmd /c npm run mqtt:mosquitto:run`

## Firmware Status

The STM32Cube G4 driver tree is now imported under `firmware/clusterstore-firmware/bsp/stm32g474/cube_generated/Drivers/`, and the ARM HAL build is wired up. The checked path is:

1. `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/check-firmware-env.ps1`
2. `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/build-g474-hal.ps1`

That produces the STM32G474 CAN bench, native node slot images, and bootloader from the portable firmware workspace.
The cross-build now uses a clean configure by default and carries local bare-metal syscall stubs, so the default ARM build path is reproducible without the earlier cache drift or `nosys` linker noise.
Keep STM32Cube in the current baseline. The imported driver tree and generated startup/system files are still required by the BSP and the working ARM build.

## Operations Runbook

See `docs/operations-runbook.md` for:

- local daemon bring-up
- the one-command local validation path through `npm run audit:full`
- firmware build workflow
- live config validation and optional endpoint probes
- live CAN, Modbus, MQTT, LTE, and cloud cutover prerequisites
- what still requires real hardware, network paths, certificates, or service credentials

See `docs/deployment-guide.md` for the strict commissioning sequence and the exact manual intervention points that still require real site inputs.
See `docs/master-implementation-walkthrough.md` for the full step-by-step implementation path, `docs/target-state-audit-2026-04-29.md` for the latest target-vs-current audit, `docs/next-development-roadmap.md` for what to build next, and `docs/local-mosquitto-setup.md` for the real local broker path.

## Immediate Product Assumptions

- Intra-cluster comms use CAN at 500 kbps.
- Grid-side inverter integration uses Modbus RTU or Modbus TCP.
- Cloud uplink uses MQTT over LTE/4G.
- The commercial MVP uses equal-current dispatch first, while keeping hooks for SoC-weighted and temperature-weighted dispatch.
- Node controllers fail safe into standalone-safe behavior if EMS supervision is lost.
- ClusterStore supports two node deployment modes:
  native STM32G474 nodes and overlay nodes that supervise existing BESS assets such as Victron, Pylontech, Growatt, or Deye units through Modbus or CAN-BMS adapters.
- The faster commercial path is the overlay-node path; both modes converge at the Cluster EMS software layer.

## Repository Layout

```text
docs/
firmware/clusterstore-firmware/
firmware/node-firmware/
packages/contracts/
services/cluster-ems/
services/utilitycore-bridge/
```

## Next Build Milestones

1. Add hardware-in-the-loop coverage for live CAN, inverter, and modem paths before field deployment.
2. Extend overlay adapters from the normalized `bms_adapter` seam into vendor-specific integrations.
3. Add secure provisioning, OTA updates, and commissioning workflows.
4. Add production certificate handling and secret injection for MQTT/cloud deployment.
5. Move from local smoke validation to site-specific commissioning checklists and rollback playbooks.
