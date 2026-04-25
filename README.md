# ClusterStore Platform

ClusterStore is the software backbone for a clustered energy storage system made of PowerHive nodes, a cluster controller, and the UtilityCore fleet platform above it.

The main planning and audit reference lives in `docs/clusterstore-master-plan.md`.

This repository seeds the full stack across three runtime layers:

1. `firmware/node-firmware` for the embedded node controller contract and cluster-aware state machine.
2. `services/cluster-ems` for the cluster controller orchestration loop running on DIN rail IPC hardware or equivalent controller hardware.
3. `services/utilitycore-bridge` for the MQTT/LTE and local SCADA bridge into UtilityCore.
4. `packages/contracts` for the shared CAN, telemetry, MQTT, and command contracts across the stack.

## What Is Included

- A capability map that turns the ClusterStore pillars into concrete build requirements.
- Shared data contracts for node telemetry, EMS control, inverter integration, and UtilityCore telemetry.
- Starter EMS orchestration code for startup equalization, dispatch allocation, fault isolation, and HMI/watchdog integration.
- Starter node firmware interfaces for CAN messaging and cluster-aware state transitions.
- A UtilityCore bridge skeleton for telemetry publishing, command intake, acknowledgements, and local SCADA fanout.

## Immediate Product Assumptions

- Intra-cluster comms use CAN at 500 kbps.
- Grid-side inverter integration uses Modbus RTU or Modbus TCP.
- Cloud uplink uses MQTT over LTE/4G.
- The commercial MVP uses equal-current dispatch first, while keeping hooks for SoC-weighted and temperature-weighted dispatch.
- Node controllers fail safe into standalone-safe behavior if EMS supervision is lost.

## Repository Layout

```text
docs/
firmware/node-firmware/
packages/contracts/
services/cluster-ems/
services/utilitycore-bridge/
```

## Next Build Milestones

1. Freeze the CAN and MQTT schemas against actual hardware limits.
2. Implement real bus drivers for STM32 CAN, Modbus RTU/TCP, LTE modem, and watchdog IO.
3. Add a simulator and hardware-in-the-loop test bench before field deployment.
4. Add secure provisioning, OTA updates, and commissioning workflows.
