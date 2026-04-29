# Master Implementation Walkthrough

This is the detailed step-by-step implementation map for the current ClusterStore baseline. It is written as the single walkthrough to reproduce, understand, and extend the repo from contracts through firmware and live deployment.

## 1. Define the System Boundary

ClusterStore is one coordinated system with four implementation layers:

1. Shared contracts under `packages/contracts`
2. Cluster controller logic under `services/cluster-ems`
3. UtilityCore and SCADA bridge logic under `services/utilitycore-bridge`
4. Native-node and bootloader firmware under `firmware/node-firmware` and `firmware/clusterstore-firmware`

The product target is:

- multiple native or overlay battery nodes
- a controller host running EMS and the UtilityCore bridge
- an inverter path over Modbus
- a cloud path over MQTT
- local HMI, watchdog, and field-service seams

## 2. Lock the Toolchain First

The first implementation step was making the workstation reproducible before treating the repo as deployable:

1. Pin Node and npm in [package.json](/C:/Users/W2industries/Downloads/clusterStore/package.json) and `.npmrc`
2. Add repeatable repo checks, builds, tests, smoke paths, and firmware commands
3. Add a one-command end-to-end audit at [scripts/full-audit.ps1](/C:/Users/W2industries/Downloads/clusterStore/scripts/full-audit.ps1)

The current repeatable entrypoint is:

- `cmd /c npm run audit:full`

## 3. Stabilize the Shared Contract Layer

Before the controller, bridge, and firmware can work together, the shared protocol has to be frozen.

Implementation sequence:

1. Align the CAN payload definitions in `packages/contracts`
2. Make serialization explicit rather than relying on raw C struct layout
3. Make tests prove round-trip compatibility between the JavaScript and firmware-facing expectations

Expected outcome:

- EMS, bridge, and native firmware all speak the same command, status, and diagnostic model

## 4. Turn the EMS into a Real Runnable Service

The controller service was previously a library scaffold. The implementation sequence for the runnable EMS is:

1. Add a real daemon entrypoint in `services/cluster-ems/src/daemon.ts`
2. Add runtime/bootstrap composition in `services/cluster-ems/src/runtime.ts`
3. Add file-backed, command-backed, and adapter-backed integration seams
4. Make config loading resolve environment placeholders such as `${VAR}` and `${VAR:-default}`
5. Add HTTP endpoints for health, snapshot, and diagnostics
6. Fix startup sequencing so brief boot-time node scarcity does not immediately latch safe shutdown
7. Add regression coverage in `tests/all.test.mjs`

Expected outcome:

- the EMS can now run as a standalone controller process, not just as an importable module

## 5. Turn the UtilityCore Bridge into a Real Runnable Service

The bridge service also needed to move from library-only to daemon-ready.

Implementation sequence:

1. Add a real daemon entrypoint in `services/utilitycore-bridge/src/daemon.ts`
2. Add runtime/bootstrap composition in `services/utilitycore-bridge/src/runtime.ts`
3. Add the TCP MQTT client in `services/utilitycore-bridge/src/mqtt-client.ts`
4. Add telemetry buffering, replay, command authorization, command ledgering, and journal paths
5. Add environment-backed secure config loading
6. Normalize LTE modem state aliases so field integrations are more tolerant

Expected outcome:

- the bridge can publish telemetry, accept remote commands, persist command outcomes, and survive LTE loss with replay

## 6. Add Local Integration Adapters So Live Configs Are Real

Live configs should not point at imaginary commands. The repo now provides real adapter seams.

Implementation sequence:

1. Add a CAN adapter CLI in [clusterstore-can-adapter.mjs](/C:/Users/W2industries/Downloads/clusterStore/scripts/clusterstore-can-adapter.mjs)
2. Add a watchdog adapter CLI in [clusterstore-watchdog-adapter.mjs](/C:/Users/W2industries/Downloads/clusterStore/scripts/clusterstore-watchdog-adapter.mjs)
3. Add config examples in:
   - [example.can-adapter.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.can-adapter.json)
   - [example.watchdog-adapter.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.watchdog-adapter.json)
4. Wire those into [example.live.daemon.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.live.daemon.json)
5. Extend live readiness validation so it checks command targets and working directories

Expected outcome:

- the live EMS example now references real repo tooling instead of placeholders

## 7. Build the Local Validation Stack

We need a fast loop before touching a live site.

Implementation sequence:

1. Add an in-process fake MQTT broker for tests
2. Add a CLI wrapper at [fake-mqtt-broker-cli.mjs](/C:/Users/W2industries/Downloads/clusterStore/scripts/fake-mqtt-broker-cli.mjs)
3. Add a runnable stack smoke test at [smoke-daemon-stack.ps1](/C:/Users/W2industries/Downloads/clusterStore/scripts/smoke-daemon-stack.ps1)
4. Add live configuration validation at [live-readiness-check.mjs](/C:/Users/W2industries/Downloads/clusterStore/scripts/live-readiness-check.mjs)
5. Add the one-command audit so the whole validation chain can be rerun quickly

Expected outcome:

- we can prove local health, MQTT publish, EMS/bridge startup, firmware host tests, and ARM firmware builds from one command

## 8. Add a Real Local Mosquitto Path

The repo already had a fake broker for test automation. For a real local operator workflow we now keep a Mosquitto path as well.

Implementation sequence:

1. Keep `services/utilitycore-bridge/config/example.daemon.json` pointed at `127.0.0.1:1883`
2. Add a local broker config at [local-dev.conf](/C:/Users/W2industries/Downloads/clusterStore/services/utilitycore-bridge/config/mosquitto/local-dev.conf)
3. Add a launcher/check script at [local-mosquitto.ps1](/C:/Users/W2industries/Downloads/clusterStore/scripts/local-mosquitto.ps1)
4. Add package scripts:
   - `npm run mqtt:mosquitto:check`
   - `npm run mqtt:mosquitto:run`

Expected outcome:

- if `mosquitto.exe` is installed or `CLUSTERSTORE_MOSQUITTO_EXE` is set, the repo can run against a real local broker instead of the fake test broker

## 9. Unblock the Native STM32 Path

The firmware work had to move from partial scaffolding to a reproducible ARM build.

Implementation sequence:

1. Create a portable firmware workspace under `firmware/clusterstore-firmware`
2. Add host-testable libraries for boot control, CRC, journal, and platform seams
3. Import the STM32Cube G4 driver tree into `bsp/stm32g474/cube_generated/Drivers/`
4. Fix the ARM toolchain file and clean-configure flow
5. Add STM32 syscall stubs so the bare-metal build does not fall back to noisy `nosys` behavior
6. Build the bench image, native node slot images, and bootloader

Expected outcome:

- the repo can now build real ARM images for the STM32G474 path on this workstation

## 10. Keep STM32Cube For Now

Yes, keep STM32Cube in the current architecture.

Reason:

1. The imported HAL driver tree and generated startup/system files are now part of the working ARM build
2. The BSP currently depends on the Cube-generated clock, interrupt, GPIO, ADC, I2C, and FDCAN bring-up surfaces
3. Removing STM32Cube now would create risk before hardware-in-the-loop validation is complete

The right strategy is:

- keep STM32Cube for the current native-node baseline
- prove the hardware path with HIL and first board bring-up
- only then decide whether some portions should migrate to LL or handwritten BSP code

## 11. Move From Local Validation to Live Commissioning

Once local audit is green, the implementation sequence becomes operational:

1. Fill the environment-backed values in the live config templates
2. Place the real broker CA file and real MQTT credentials
3. Point the CAN and watchdog adapter configs at real site paths or real local commands
4. Validate with `npm run check:live-readiness`
5. Probe live dependencies with `node scripts/live-readiness-check.mjs --probe`
6. Start the EMS
7. Start the bridge
8. Commission CAN, Modbus, MQTT, LTE, watchdog, and cloud paths on site
9. Flash and validate the STM32 images on hardware

## 12. The Practical Rule Going Forward

Do not treat "tests pass" as equivalent to "site ready."

The system is only fully operational when:

- audit and smoke checks are green
- live-readiness warnings are cleared with real credentials and certificates
- CAN and Modbus paths are proven on the actual site hardware
- MQTT round-trip and command acknowledgements are proven against the real broker
- LTE outage and replay behavior are exercised on the real modem path
- STM32 bootloader and runtime images are flashed and validated on hardware
