# ClusterStore Master Plan

## Purpose

This document is the single planning reference for ClusterStore across node firmware, the Cluster EMS, the UtilityCore bridge, and the fleet layer above it.

It consolidates:

- System architecture
- Capability scope
- Production-readiness audit findings
- Required features
- Integrations
- Innovation opportunities
- Delivery phases
- Pilot and rollout exit criteria

## System Summary

ClusterStore is the software-defined control and operations layer for a clustered battery system made up of multiple PowerHive nodes, a cluster controller, and the UtilityCore platform above it.

The stack has three core runtime layers:

1. Node-level firmware or overlay adapters at each node edge
2. Cluster EMS on the cluster controller or DIN rail industrial PC
3. Communications and fleet bridge into UtilityCore / MAAP

## Architecture

### 1. Node-Level Firmware

ClusterStore supports two node deployment modes that converge at the Cluster EMS layer:

- `Mode A: Native node`
  An STM32G474RET6 node runs the portable firmware core directly on ClusterStore hardware.
- `Mode B: Overlay node`
  An adapter wraps an existing BESS asset such as Victron, Pylontech, Growatt, or Deye through its native Modbus or CAN-BMS protocol and exposes ClusterStore node semantics upward.

For the native path, each PowerHive node is responsible for:

- Local BMS protection and enforcement
- Local MPPT or converter control
- CAN heartbeat, status, and diagnostic reporting
- Receiving EMS commands and enforcing them within local safety limits
- Falling back to standalone-safe behavior if EMS supervision is lost

Cluster-specific node behavior includes:

- `NODE_STATUS` reporting
- `NODE_CMD` handling
- `NODE_DIAG` diagnostics reporting
- Cluster-aware operating states:
  - `CLUSTER_SLAVE_CHARGE`
  - `CLUSTER_SLAVE_DISCHARGE`
  - `CLUSTER_BALANCING`
  - `CLUSTER_ISOLATED`

### 2. Cluster EMS

The Cluster EMS is the coordination brain. It is responsible for:

- Polling or receiving node state over CAN
- Reading inverter and site state over Modbus RTU/TCP
- Managing startup equalization before shared bus operation
- Allocating charge and discharge current across nodes
- Isolating faulty nodes and keeping the cluster running in degraded mode
- Driving the local HMI
- Feeding the watchdog and triggering safe behavior when control health is lost

### 3. UtilityCore Bridge

The UtilityCore bridge is the upward integration layer. It is responsible for:

- MQTT telemetry publishing
- Immediate alarm publishing
- Receiving remote commands from UtilityCore
- Buffering telemetry during LTE outages
- Replaying buffered telemetry on reconnect
- Exposing local SCADA integrations over Modbus TCP/RTU or Ethernet

## Core Capability Scope

### Node Firmware Capabilities

- BMS enforcement and local protection
- MPPT / converter control
- CAN status and command interface
- Cluster-aware state machine
- EMS supervision timeout fallback
- Node isolation support
- Dual-slot boot control and non-volatile event journaling on native nodes
- Adapter normalization for overlay nodes so existing BESS assets can participate in the same EMS control model

### Cluster EMS Capabilities

- Node registry and health tracking
- Startup SoC equalization
- Equal-current dispatch
- SoC-weighted dispatch
- Temperature-weighted dispatch
- Fault isolation and degraded mode
- Modbus inverter integration
- Local HMI / field diagnostics
- Hardware watchdog integration
- Maintenance mode
- Commissioning mode
- Precharge and contactor interlock logic
- Local event journal

### Communications Capabilities

- MQTT telemetry uplink
- MQTT alerts uplink
- Remote command subscription
- Command acknowledgement flow
- Local SCADA integration
- Telemetry store-and-forward
- Secure identity and MQTT authentication
- LTE modem observability

### Fleet / UtilityCore Capabilities

- ClusterStore asset model
- Per-node SoC visualization
- Aggregate energy and power curves
- Fault event history
- Cycle analytics and degradation tracking
- Remote dispatch optimization
- Predictive fault detection

## Current Production-Readiness Assessment

### Overall Verdict

The repository is not yet production-ready. It currently represents a strong architecture starter kit rather than a field-deployable system.

The biggest risks are no longer the original contract and startup skeleton problems. The biggest remaining risks are:

1. Hardware binding and convergence between the new portable firmware core and the older STM32 runtime path
2. Production-grade security provisioning and credential lifecycle
3. Billing-grade metering and reconciliation
4. HIL validation and field commissioning workflows

## Audit Findings

### Critical Findings Addressed In Current Baseline

1. The CAN contract is now aligned across the TypeScript contracts, CAN schema, and embedded firmware definitions through explicit 8-byte wire payload encoding and decoding.
2. The EMS now implements startup equalization with DC bus precharge, ordered node admission, voltage-window checks, and contactor closure confirmation.
3. The remote command path now includes target validation, authorization metadata checks, idempotency handling, sequence ordering, and EMS-side safety validation before application.

### Remaining High-Risk Work

1. Telemetry buffering and acknowledgement flow exist in software, but production still needs a persistent backing store on controller hardware rather than an in-memory implementation.
2. Fault incidents are deduped and latched in the EMS, but a full operator acknowledgement workflow in UtilityCore is still outstanding.
3. EMS supervision timeout handling is centralized in configuration, but the firmware-side default still needs to be bound to site configuration at deployment time.
4. Aggregate SoC and power accounting now prefer fresh-node data, preserve the last good aggregate snapshot, and prefer site metering when available, but billing-grade accuracy still requires calibrated meter integration and long-horizon reconciliation.
5. Firmware wire serialization is explicit and no longer relies on raw structs, but the STM32 target build and HIL validation still need to prove the implementation on real hardware.
6. The new portable firmware core now host-builds and passes tests, but the native STM32 images still link through the older `firmware/node-firmware` runtime path, so target/runtime convergence is still outstanding.

### Medium Findings

1. Some EMS states exist in the model but are not yet fully operationalized.
2. There is no simulator, no HIL bench, no CI test gate, and no field commissioning workflow.

## Production Gaps By Subsystem

### Node Firmware Baseline Now In Repo

- Explicit CAN serialization and deserialization for status, command, and diagnostic frames
- Command freshness handling with supervision timeout enforcement
- Sequence counters and heartbeat counters in the node controller diagnostics surface
- Contactor state machine with precharge sequencing
- Welded-contactor detection and latch behavior
- Current ramp limiting for charge and discharge transitions
- Local rejection of unsafe cluster commands based on limits, lockouts, and balancing policy
- OTA candidate, trial, confirm, and rollback state tracking
- Event journal hooks for non-volatile persistence
- Maintenance and service lockout flows in the node state machine and status flags
- A portable native-node firmware workspace scaffold under `firmware/clusterstore-firmware`
- Host-testable boot control, CRC32, flash journal, and platform vtable modules aligned to the STM32G474 memory map

### Remaining Node Firmware Production Work

- Bind the portable firmware core to the real STM32G474 BSP for FDCAN, GPIO contactors, INA228/current sensing, ADC telemetry, and IWDG handling
- Collapse the duplicated native-node runtime so the code validated in `firmware/clusterstore-firmware/lib/` is the same code linked into the STM32 images
- Add the board bootloader and node application images on top of the converged portable workspace
- Build the first CAN-only bench with a NUCLEO-G474RE and USB-CAN adapter
- Add overlay-node adapters for Victron, Pylontech, Growatt, and Deye protocols where early commercial deployments benefit from reusing existing BESS hardware

### Cluster EMS Gaps

- Persistent node registry with capabilities and health
- Brownout and black-start recovery
- Grid-flap handling
- State persistence after controller reboot
- Alarm lifecycle management
- Safety supervisor separated from economic dispatch
- Commissioning workflow
- Per-site operational envelopes and configuration bundles

### Bridge Gaps

- Secure bootstrap and device provisioning
- TLS mutual auth and credential lifecycle
- Durable outbound queue with replay checkpoints
- End-to-end command lifecycle tracking
- Command audit logging
- Local diagnostics API for technicians
- Bridge health metrics
- Remote config sync with rollback

### Fleet Platform Gaps

- ClusterStore asset type in UtilityCore
- Dedicated dashboards
- Incident workflow and alert escalation
- Historical analytics and degradation modelling
- Policy engine for tariff-aware and backup-aware dispatch
- Maintenance records and service history

## Minimum Features Before First Pilot

The following should be complete before energizing a real pilot site:

1. Frozen CAN wire protocol with explicit serialization
2. Frozen Modbus profile for the first supported inverter family
3. Safe equalization and contactor sequencing
4. Independent safety supervisor and watchdog behavior
5. Durable telemetry and event storage on the controller
6. Secure and auditable remote command path
7. Commissioning workflow and maintenance lockout
8. Alarm dedupe, latch, acknowledge, and clear lifecycle
9. Local HMI sufficient for offline field diagnostics
10. Simulator and HIL coverage for key failure modes

## Minimum Features Before Broad Commercial Rollout

1. OTA with signed artifacts and rollback
2. Configuration management and staged rollout controls
3. Strong observability and fleet health monitoring
4. Predictive analytics and degradation models
5. Multi-inverter support
6. Field service tooling
7. Security baseline suitable for industrial deployment
8. SLA-grade UtilityCore operations workflows

## Required Integrations

### Priority Integrations

1. First inverter family over Modbus
2. Secure MQTT broker integration with UtilityCore
3. Local SCADA register map over Modbus TCP/RTU
4. LTE modem management and health monitoring
5. Metering integration for accurate reconciliation

### Secondary Integrations

1. Technician service tool over USB, Ethernet, or Wi-Fi AP mode
2. Environmental sensors such as cabinet temperature, humidity, smoke, and door state
3. Generator / ATS integration where hybrid sites require it
4. Digital IO for E-stop, fire alarm, and tamper handling

## Innovation Opportunities

These are the areas that could make ClusterStore differentiated in market terms.

### 1. Self-Healing Cluster Control

- Automatic node quarantine
- Dynamic rebalancing after fault recovery
- Cluster resilience scoring

### 2. Digital Twin and Replay

- Site replay engine for postmortem analysis
- Simulation-backed commissioning
- Fleet anomaly library built from field events

### 3. Economic Intelligence

- Tariff-aware dispatch
- Solar/load forecasting
- Backup-reserve optimization
- Export optimization

### 4. Predictive Reliability

- Contactor wear estimation
- Thermal stress modelling
- Resistance drift monitoring
- Early warning on recurring transient faults

### 5. Offline-First Field Operations

- Full local technician mode
- QR-guided commissioning and node replacement
- Automatic maintenance service reports

### 6. Fleet-Level MAAS Advantage

- Treat all deployed clusters as one policy-driven fleet
- Support analytics-backed MAAS service tiers
- Enable future VPP participation through MAAP

## Recommended Delivery Phases

### Phase 0: Safety and Contract Freeze

1. Split wire payloads from normalized platform models
2. Freeze binary CAN encoding
3. Freeze command semantics and expiry behavior
4. Design startup equalization / precharge state machine
5. Define remote-command security model

### Phase 1: Edge Runtime Hardening

1. Implement real CAN, Modbus, LTE, watchdog, and storage adapters
2. Add durable message queue and event journal
3. Add alarm lifecycle management
4. Add commissioning and maintenance flows

### Phase 2: Verification Infrastructure

1. Build simulator for nodes, inverter, solar, grid, LTE, and operator actions
2. Add replay tests from recorded scenarios
3. Add HIL tests for contactor sequencing and watchdog failure
4. Add CI checks for protocol, EMS, and bridge behavior

### Phase 3: Pilot Product

1. Support one inverter family deeply
2. Support secure MQTT to UtilityCore
3. Deliver field technician HMI and local diagnostics
4. Deliver commissioning and remote support basics

### Phase 4: Commercial Differentiation

1. Add advanced dispatch and predictive analytics
2. Add OTA and staged rollout
3. Add service tooling and reporting
4. Add fleet-level optimization and MAAS packaging

## Exit Criteria

### Ready For Pilot

- CAN, Modbus, and MQTT contracts are frozen and tested
- Startup equalization and fault isolation pass HIL tests
- LTE outage and replay behavior is proven
- Local operators can commission and isolate nodes safely
- Remote commands are bounded, auditable, and secure

### Ready For Commercial Rollout

- OTA and rollback are working
- Fleet observability is live
- Alert fatigue is controlled through dedupe and lifecycle handling
- Config rollout and staged release controls exist
- UtilityCore can support analytics-backed MAAS operations

## Immediate TODO Actions

1. Import the STM32Cube G4 `Drivers/` tree and produce the first HAL-backed ARM build for `cs_can_bench_g474`
2. Converge the native STM32 app and bootloader targets onto the new portable firmware modules instead of the older duplicated runtime path
3. Replace in-memory queues and journals with persistent controller storage and replay checkpoints
4. Add secure device provisioning, credential rotation, and signed OTA artifact handling
5. Stand up HIL coverage for contactor sequencing, stale-command fallback, modem outages, and controller reboot recovery
6. Build commissioning and service workflows that exercise the lockout, diagnostics, and rollback paths end to end
7. Stand up overlay-node adapters so early deployments can supervise existing BESS assets while the PowerHive-native path matures

## Source Documents

This master plan consolidates and supersedes the planning context spread across:

- `README.md`
- `docs/architecture.md`
- `docs/capability-map.md`
- `docs/roadmap.md`
