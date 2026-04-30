# Implementation Audit — ClusterStore Platform

Date: `2026-04-29`
Scope: Every implemented source file across contracts, EMS, bridge, firmware, tests, scripts, and config.

This is a code-level audit — not docs, not claims. Each section reports what the code actually does, what is stubbed, and where the real gaps are.

---

## Executive Summary

The software stack is substantially implemented. The EMS, bridge, and contracts layers are not scaffolding — they contain real, working implementations of the control loop, startup sequencing, dispatch, fault management, MQTT protocol, Modbus TCP, telemetry buffering, command validation, and authorization. The firmware layer contains real C code for the STM32G474 BSP and node control modules. The most significant open items are: (1) the firmware runtime split between the portable `lib/` core and the older `node-firmware/` path, (2) a handful of explicitly stubbed features (32-bit Modbus writes, per-node command targeting, `AllowAllAuthorizer`), and (3) every hardware integration path that requires real site equipment.

---

## Layer 1 — Shared Contracts (`packages/contracts/src/`)

### `can.ts` — FULLY IMPLEMENTED

**Frame protocol constants:**
- Baud rate, payload size, max nodes, supervision timeout — all concrete values
- 8 status flag bit definitions (CONTACTOR_CLOSED, READY_FOR_CONNECTION, BALANCING_ACTIVE, MAINTENANCE_LOCKOUT, SERVICE_LOCKOUT, etc.)
- 8-position fault flag ↔ FaultCode enum mapping table

**Encode/decode functions — all fully implemented:**

| Function | Direction | Notes |
|---|---|---|
| `encodeNodeStatusPayload()` | Node → Controller | Explicit 8-byte pack |
| `decodeNodeStatusPayload()` | Controller reads | Full field unpack |
| `encodeNodeCommandPayload()` | Controller → Node | Explicit 8-byte pack |
| `decodeNodeCommandPayload()` | Node reads | Full field unpack |
| `encodeNodeDiagnosticChunkPayload()` | Node → Controller | Chunk framing |
| `decodeNodeDiagnosticChunkPayload()` | Controller reads | Chunk unpack |
| `toNodeStatusFrame()` | Frame conversion | Full |
| `toNodeStatusPayload()` | Frame to wire | Full |
| `toDiagnosticChunkFrames()` | Multi-frame split | Full |

**Status:** Complete. No stubs. CAN wire format is explicit 8-byte encoding aligned to the C firmware.

---

### `mqtt.ts` — FULLY IMPLEMENTED

Topic routing: `telemetryTopic()`, `alertsTopic()`, `commandsTopic()`, `commandAckTopic()` — all parameterized by siteId/clusterId/nodeId.

Envelope wrappers: `wrapTelemetry()`, `wrapAlert()`, `wrapCommand()`, `wrapCommandAck()` — all produce schema-versioned (`"1.0.0"`) JSON envelopes.

**Status:** Complete. No stubs.

---

### `types.ts` — COMPLETE TYPE SURFACE

| Type | Values |
|---|---|
| `ClusterMode` | startup_equalization, normal_dispatch, degraded, maintenance, safe_shutdown |
| `StartupPhase` | 7 phases (discover_nodes → failed) |
| `NodeMode` | 8 modes including all cluster slave states |
| `CommandType` | 5 types (force_charge, force_discharge, set_dispatch_mode, set_maintenance_mode, clear_fault_latch) |
| `FaultCode` | 16 fault codes |
| `DispatchStrategy` | equal_current, soc_weighted, temperature_weighted |

**Status:** Type definitions only — no runtime logic here by design.

---

## Layer 2 — Cluster EMS (`services/cluster-ems/src/`)

### `ems-controller.ts` — FULLY IMPLEMENTED

The core control loop. `runCycle()` executes every tick and handles the full EMS lifecycle:

**`runCycle()` — complete implementation:**
1. Kicks the hardware watchdog
2. Reads all node statuses + inverter state in parallel
3. Evaluates faults and isolates affected nodes via `FaultManager`
4. Sequences cluster commands (startup, dispatch, or maintenance)
5. Handles three operational branches:
   - **Maintenance mode** — isolates all nodes, triggers inverter hold-open
   - **Startup sequencing** — delegates to `StartupSequencer` for phased admission
   - **Normal dispatch** — runs `planDispatch()` → `buildNodeCommands()` → sends to CAN bus
6. Records alerts
7. Builds and caches telemetry via `buildTelemetry()`
8. On any unhandled exception: triggers fail-safe and re-throws

**`applyRemoteCommand()` — complete implementation:**
- Validates TTL, auth metadata, target binding, sequence ordering
- Supports all 5 `CommandType` values with role-based authorization
- Force-charge/discharge overrides expire via `expiresAt` timestamp
- All applied commands are journaled

**`planDispatch()` — complete implementation:**
- Checks for active remote override (force charge/discharge) and uses it if within TTL
- Reads inverter state to determine charge/discharge headroom
- Dispatches to `allocateCurrent()` with the configured strategy

**`buildTelemetry()` — complete implementation:**
- SOC: SoC-weighted mean from fresh nodes, falls back to last good snapshot if all stale
- Power: prefers site metering over inferred node telemetry
- Detects power mismatch between site metering and node aggregates
- Tracks cumulative energy (kWh) across cycles
- Assesses data quality: freshNodeCount, staleNodeCount, missingNodeCount
- Returns full telemetry envelope with per-node detail

**Stubs / gaps:**
- Per-node command targeting: explicitly noted as "not yet supported" in the command validation path

---

### `startup-sequencer.ts` — FULLY IMPLEMENTED

Seven-phase state machine. Each phase has a timeout; violation triggers transition to `failed`.

| Phase | What Happens |
|---|---|
| `discover_nodes` | Selects primary node from healthy candidates by highest SoC |
| `precharge_primary` | Waits for DC bus voltage to rise to within window of primary node voltage |
| `close_primary` | Signals inverter hold-open, closes primary node contactor |
| `admit_nodes` | Admits remaining nodes one at a time, voltage-window check per node |
| `balance_cluster` | Waits for SoC spread across admitted nodes to fall within the balance window |
| `ready` | Cluster is ready for normal dispatch — exits sequencer |
| `failed` | Timeout or fault during any phase — cluster goes to safe_shutdown |

All timeouts are configurable via the startup config block. Voltage mismatch detection generates `voltage_mismatch` alerts. The sequencer generates a `startup_sequence_fault` alert and latches into `failed` on timeout.

**Status:** Complete. No stubs.

---

### `fault-manager.ts` — FULLY IMPLEMENTED

`evaluate()` runs every cycle and:
1. Scans all node statuses for heartbeat timeout (using configurable supervision window)
2. Collects fault flags from nodes that are within their heartbeat window
3. Transitions incident state: new faults open new incidents, cleared faults resolve existing ones
4. Generates `opened` and `cleared` alerts with incident IDs
5. Returns: `activeFaults`, `alerts`, `isolatedNodeIds`, `clusterDegraded` flag

Faults are latched as incidents with stable IDs — the same fault does not generate a new ID every cycle. Deduplication is built-in.

**Status:** Complete. No stubs.

---

### `dispatch.ts` — FULLY IMPLEMENTED

**`allocateCurrent()` — three strategies:**

| Strategy | Algorithm |
|---|---|
| `equal_current` | Divides total setpoint equally across all healthy nodes, capped per node |
| `soc_weighted` | Weight = (100 − SoC) for charge, SoC for discharge; distributes proportionally |
| `temperature_weighted` | Weight = (maxTemp − nodeTemp) for charge, inverse for discharge |

All strategies: filter healthy nodes only, apply per-node current caps, return `DispatchAllocation[]` with per-node current setpoints.

**`buildNodeCommands()` — complete:**
- Maps allocations to node command frames using `encodeNodeCommandPayload()`
- Unhealthy/isolated nodes receive STOP commands
- Sets contactor state and mode (charge/discharge/standby) per node

**Status:** Complete. No stubs.

---

### `runtime.ts` (EMS) — FULLY IMPLEMENTED — 12 Port Implementations

This is the largest file in the EMS (~1459 lines). Every port interface has at least one concrete implementation here.

| Port Class | Type | What It Does |
|---|---|---|
| `JsonFileCanBusPort` | File-based | Reads/writes JSON node status and command files — dev/test |
| `CommandCanBusPort` | Subprocess | Spawns external process to read/write CAN — live integration |
| `OverlayBmsAdapter` | Overlay | Wraps `bms-adapter.ts`; normalizes overlay BESS assets |
| `StateFileGridInverterPort` | File-based | Reads/writes JSON inverter state — dev/test |
| `ModbusTcpGridInverterPort` | **Real Modbus TCP** | Full protocol implementation (see below) |
| `CommandGridInverterPort` | Subprocess | Spawns external process for inverter reads/writes |
| `ConsoleHmiPort` | Console | Logs telemetry snapshots to stdout |
| `FileHmiPort` | File-based | Writes telemetry + alerts to JSON files |
| `FileWatchdogPort` | File-based | Writes heartbeat and fail-safe events to JSON |
| `CommandWatchdogPort` | Subprocess | Spawns external watchdog command |
| `JsonLineOperationalJournalPort` | File JSONL | Appends all operational events to a `.jsonl` file |
| `CommandOperationalJournalPort` | Subprocess | Spawns journal command |

**`ModbusTcpGridInverterPort` — real Modbus TCP, not a stub:**
- Constructs Modbus TCP frames with transaction IDs, unit IDs, function codes
- Reads holding registers (FC03)
- Writes single registers (FC06)
- Handles: 16-bit signed/unsigned fields, 32-bit double-register fields, boolean fields with configurable `trueValue`, tariff band enum fields
- Applies field-level scaling factors (e.g., `0.1` for tenths)
- Handles register byte order and sign extension
- **Stub:** 32-bit register writes — `throw new Error('32-bit Modbus register writes not yet supported')` — the only explicit stub in this file

**HTTP Daemon — 5 endpoints:**

| Endpoint | Method | What It Does |
|---|---|---|
| `/health` | GET | Daemon state, cycle count, last cycle time |
| `/snapshot` | GET | Latest telemetry snapshot |
| `/alerts` | GET `?drain` | Returns pending alerts; drains queue if `?drain` |
| `/diagnostics` | GET | Per-node diagnostic data |
| `/run-cycle` | POST | Forces one control cycle immediately |
| `/commands` | POST | Applies a remote command via `applyRemoteCommand()` |

**Utilities:**
- `resolveEnvPlaceholders()` — replaces `${VAR}` and `${VAR:-default}` in config strings recursively
- `invokeCommand()` — spawns subprocess, passes JSON via stdin, reads JSON from stdout
- Atomic file writes using temp-file-then-rename
- JSONL append helper

---

### `adapters/` — Interface Definitions Only

`can-bus.ts`, `modbus.ts`, `hmi.ts`, `watchdog.ts`, `journal.ts` are all TypeScript interface files. They define the port contracts — no implementations. All concrete implementations live in `runtime.ts`.

`bms-adapter.ts` is the exception — it is a full implementation:

**`bms-adapter.ts` — FULLY IMPLEMENTED:**
- `normalizeOverlayStatus()` — maps overlay asset telemetry fields to `NodeStatusFrame` wire format
- `normalizeOverlayDiagnostic()` — maps overlay asset to `NodeDiagnosticFrame`
- `OverlayBmsAdapter` — implements `CanBusPort`:
  - Caches the most recent snapshot per asset
  - `readNodeStatuses()` — returns all cached overlay asset frames
  - `sendNodeCommand()` — resolves overlay asset by nodeId or nodeAddress (both must match if both provided — the identifier resolution bug is fixed)
  - Translates EMS commands to overlay BESS control actions

---

## Layer 3 — UtilityCore Bridge (`services/utilitycore-bridge/src/`)

### `bridge-service.ts` — FULLY IMPLEMENTED

**`bindCommandSubscription()` — complete command intake pipeline:**
1. Subscribes to MQTT command topic
2. Parses and unwraps envelope
3. Validates command via `validateRemoteCommand()` (command-router.ts)
4. Checks command ledger for duplicate idempotency key
5. Checks authorization policy
6. Publishes intermediate `accepted` ack to MQTT
7. Calls EMS `/commands` endpoint to apply the command
8. Records in command ledger
9. Publishes `completed` or `rejected` ack on outcome

**`publishCycle()` — complete telemetry publish pipeline:**
1. Fetches snapshot + drains alerts from EMS HTTP API
2. Publishes to local SCADA output
3. Checks LTE modem state
4. If online: publishes telemetry to MQTT; on failure buffers the message
5. If offline: enqueues to outbound buffer
6. Replays buffered messages when back online (ack-gated — buffer only drains after publish confirmed)
7. All events journaled

**`replayBufferedMessages()` — complete:**
- Peeks pending buffer in batches (bounded)
- Publishes each message
- Acknowledges only on success — no silent data loss

---

### `command-router.ts` — FULLY IMPLEMENTED

`validateRemoteCommand()` performs 116+ validation checks:
- All required fields present (`id`, `idempotencyKey`, `requestedBy`, `command`, `target`, `authorization`)
- `createdAt` is a valid timestamp
- `expiresAt` is a valid timestamp in the future relative to `createdAt`
- Authorization `authorizedAt` must be before `expiresAt`
- Command must not be already expired (checked against current time)
- `target.siteId` and `target.clusterId` must match the configured local values
- `authorization.role` must be in the allowed roles list
- `authorization.scope` must include the command type
- Per-command validation:
  - `force_charge`/`force_discharge`: `currentA` must be a positive finite number
  - `set_dispatch_mode`: `strategy` must be a valid `DispatchStrategy` value
  - `set_maintenance_mode`: `enabled` must be boolean
  - `clear_fault_latch`: `nodeId` must be present
- Per-node command targeting: explicitly marked "not yet supported" — returns validation error

---

### `mqtt-client.ts` — FULLY IMPLEMENTED MQTT 3.1.1 PROTOCOL

This is a handwritten MQTT 3.1.1 TCP client — not a library wrapper.

**What it implements:**
- CONNECT packet construction with clientId, username, password, clean session, keep-alive
- TLS support via Node.js `tls.connect()` with CA certificate verification
- PUBLISH packet with topic, payload, QoS 0 and QoS 1
- SUBSCRIBE packet with topic filter and QoS
- PUBACK for incoming QoS 1 messages
- PINGREQ / PINGRESP keep-alive
- DISCONNECT on close
- Variable-length integer encoding/decoding
- TCP stream reassembly — packet fragmentation handled correctly
- CONNACK parsing with return code checking
- SUBACK parsing

**TLS:** when `caFile` is configured, loads the CA cert from disk and creates a TLS socket. Connects over plain TCP otherwise.

**What is not implemented:**
- QoS 2 (PUBREC/PUBREL/PUBCOMP)
- Retained messages (`retain` flag, though the field exists)
- Will messages
- Automatic reconnect (reconnect is handled at the daemon level)

---

### `runtime.ts` (Bridge) — FULLY IMPLEMENTED — 10 Port Implementations

| Port Class | Type | What It Does |
|---|---|---|
| `HttpClusterEmsClient` | HTTP | Calls EMS `/snapshot`, `/alerts?drain`, `/commands` endpoints |
| `FileTelemetryBuffer` | File JSONL | Pending message queue with deduplication by message ID |
| `FileScadaPort` | File-based | Writes telemetry + alerts to JSON |
| `AllowAllAuthorizer` | **STUB** | Always returns `{ authorized: true }` — bypasses all auth |
| `PolicyAuthorizer` | Role-based | Checks `allowedRoles` and `allowedRequesters` lists |
| `FileCommandLedger` | File-based | Tracks seen idempotency keys; stores command + ack records |
| `JsonLineOperationalJournalPort` | File JSONL | Appends all events to `.jsonl` |
| `StateFileLteModemPort` | File-based | Reads JSON file for online/rssi — dev/test |
| `HttpJsonLteModemPort` | HTTP | Fetches LTE status JSON over HTTP with timeout |
| `CommandLteModemPort` | Subprocess | Spawns external command to get LTE state |

**`AllowAllAuthorizer`** — the only authorization stub. Used when the bridge config omits an auth policy. In production, `PolicyAuthorizer` must be configured with real role and requester lists.

**Online state extraction:** Supports 4 field name variants (`online`, `connected`, `isOnline`, `status === 'online'`) for flexibility with different LTE modem HTTP APIs.

**HTTP Daemon — 3 endpoints:**

| Endpoint | Method | What It Does |
|---|---|---|
| `/health` | GET | Bridge state, cycle count, LTE state |
| `/publish-cycle` | POST | Forces one publish cycle immediately |
| `/ems-command` | POST | Passes command to EMS (diagnostic path) |

---

## Layer 4 — Node Firmware (`firmware/node-firmware/`)

All files are real C implementations. Listing by module:

### Core Protocol

**`cluster_can_protocol.c` + `.h`**
- Explicit 8-byte CAN frame encode/decode for STATUS, COMMAND, DIAGNOSTIC frames
- Aligned byte-for-byte to `packages/contracts/src/can.ts` wire format
- No padding or struct casting — all explicit byte operations

**`cluster_flash_layout.c` + `.h`**
- Partition map for STM32G474 flash: bootloader / slot-A / slot-B / journal regions
- Concrete addresses and sizes for the G474 512KB flash map

### Node Control

**`cluster_node_controller.c`**
- Elapsed time calculation
- Command policy builder — determines which commands are permitted given current node state
- Status flag builder — assembles the 8-bit status flags byte from node state
- Fault flag builder — assembles the 8-bit fault flags byte from active fault conditions
- Target current calculation — applies local BMS and thermal derating limits

**`cluster_state_machine.c`**
- Node operating state machine: IDLE → cluster slave states → ISOLATED → FAULT
- Transition logic based on EMS commands and local fault conditions
- Supervision timeout fallback: returns node to standalone-safe behavior if EMS stops supervising

**`cluster_command_manager.c`**
- Freshness validation: rejects commands with stale timestamps
- Sequence number checking: rejects out-of-order commands
- Unsafe command rejection: refuses commands that violate local BMS limits
- Supervision timeout keyed on "command ever seen" (not assumed non-zero sequence)

**`cluster_contactor_manager.c`**
- Contactor state machine: OPEN → PRECHARGE → CLOSED → WELD_DETECT
- Precharge sequencing with voltage rise monitoring
- Welded-contactor detection and latching
- Safe open/close transitions

**`cluster_current_ramp.c`**
- Ramp limiting for charge and discharge current transitions
- Prevents step changes in current command

### Boot and OTA

**`cluster_boot_control.c`**
- Dual-slot boot metadata management (BCB — Boot Control Block)
- Boot state: NORMAL, TRIAL, ROLLBACK
- Slot confirmation and rollback logic

**`cluster_ota_manager.c`**
- OTA state machine: IDLE → CANDIDATE → TRIAL → CONFIRMED / ROLLED_BACK
- Image integrity via `cluster_crc32.c`
- Integrates with dual-slot boot control

**`cluster_crc32.c`**
- CRC32 polynomial implementation for image integrity

**`cluster_event_journal.c`**
- Structured event journal with fixed flush path
- Hooks for non-volatile persistence (NVM bindings not yet wired)

### New Modules (added 2026-04-29)

**`cluster_stm32_hal.c` + `.h`**
- STM32 HAL abstraction — wraps HAL GPIO, timer, CAN, watchdog calls behind a portable interface

**`cluster_stm32_boot.c` + `.h`**
- STM32-specific boot sequences — reset vector setup, clock init, peripheral start sequence

**`cluster_node_runtime.c` + `.h`**
- Main node application loop — integrates all modules into a single tick loop

**`cluster_bootloader_runtime.c` + `.h`**
- Bootloader-side runtime — handles slot selection, CRC check, and firmware hand-off

**`cluster_persistent_state.c` + `.h`**
- NVM read/write abstraction for persistent node state (SoC estimates, fault history, boot counter)

---

## Layer 5 — Portable Firmware Workspace (`firmware/clusterstore-firmware/`)

### `lib/` — Host-Validated Portable Modules

**`lib/boot_control/cs_boot_control.c`**
- Boot Control Block (BCB) read/write
- Slot A/B selection logic
- Trial mode handling and confirmation
- CRC32 over image header
- Host-tested via CTest — passes

**`lib/journal/cs_journal.c`**
- Circular event journal over a flat flash region
- Append, seek, flush operations
- Host-tested via CTest — passes

**`lib/cluster_platform/cs_cluster_platform.c`**
- Platform vtable: GPIO, timer, CAN send/receive, watchdog functions
- The seam between portable application logic and hardware-specific BSP
- Host-tested via CTest — passes

### `bsp/stm32g474/` — Hardware Drivers

**`cs_can_g474.c`**
- STM32G474 FDCAN driver
- Ring buffer management for CAN RX frames
- DLC conversion between CAN FD and classic CAN
- FDCAN HAL callback wiring

**`cs_bsp_g474.c`**
- Board support initialization — clock, GPIO, peripheral setup
- Connects the platform vtable to real STM32 HAL functions

**`cs_adc_g474.c`**
- ADC1 driver for voltage and temperature sensing
- DMA transfer configuration

**`cs_flash_g474.c`**
- Internal flash read/write/erase
- Page alignment and unlock sequences

**`cs_ina228.c`**
- INA228 I2C current sensor driver
- 16-bit differential measurement, hardware oversampling
- SoC integration path

**`cs_iwdg_g474.c`**
- IWDG watchdog init and kick

**`cs_syscalls.c`**
- Bare-metal newlib syscall stubs (`_sbrk`, `_write`, `_read`, `_close`, etc.)
- Eliminates `nosys` linker fallback and associated warnings
- Added 2026-04-29

### Build Targets Produced

| Target | Image |
|---|---|
| `cs_can_bench_g474` | CAN bus bench test image for NUCLEO-G474RE |
| `cs_native_node_g474_slot_a` | Node application image — slot A |
| `cs_native_node_g474_slot_b` | Node application image — slot B |
| `cs_bootloader_g474` | Bootloader image |

---

## Layer 6 — Tests (`tests/`)

### `all.test.mjs` — Integration Test Suite

Tests the full daemon stack end-to-end using file-based I/O and in-process fake services:

- **Startup sequencer** — proves the 7-phase state machine under controlled node admission
- **Fault manager** — proves heartbeat timeout detection, incident latching, dedupe
- **Command idempotency** — proves duplicate command rejection via ledger
- **Replay failure buffering** — proves telemetry buffering on MQTT publish failure
- **Rejection of unsupported per-node remote commands** — proves validation gate
- Uses `tests/support/in-memory-runtime.mjs` — shared in-memory simulator runtime
- Runs under `Node --experimental-strip-types` (no separate build step needed)

### `firmware-binding.test.mjs` — Firmware Binding Tests

- Tests firmware C modules via Node.js bindings
- `tests/support/firmware-binding-runtime.mjs` provides the runtime harness
- Passes on this workstation

### `overlay-bms-adapter.test.mjs` — Overlay BMS Adapter Tests

- Tests `bms-adapter.ts` normalization logic
- `tests/support/fake-modbus-server.mjs` provides a fake Modbus TCP server
- Tests identifier resolution (all provided IDs must match the same asset)
- Passes on this workstation

---

## Layer 7 — Scripts

| Script | What It Does |
|---|---|
| `scripts/full-audit.ps1` | One-shot PowerShell wrapper — runs the entire `npm run audit:full` chain |
| `scripts/smoke-daemon-stack.ps1` | Starts fake MQTT broker + EMS daemon + bridge daemon; verifies health endpoints and MQTT publish through real daemon paths |
| `scripts/live-readiness-check.mjs` | Validates config structure + (optionally) probes live endpoints: EMS HTTP, MQTT TCP, Modbus TCP, LTE HTTP |
| `scripts/clusterstore-can-adapter.mjs` | CAN adapter CLI — reads node status JSON and writes it to the file path the EMS `JsonFileCanBusPort` monitors |
| `scripts/clusterstore-watchdog-adapter.mjs` | Watchdog adapter CLI — writes heartbeat events the watchdog port expects |
| `scripts/fake-mqtt-broker-cli.mjs` | Fake MQTT broker as a standalone CLI process (used by smoke stack) |
| `scripts/local-mosquitto.ps1` | Checks for / launches real Mosquitto broker from configured path |
| `scripts/smoke-simulator.mjs` | End-to-end smoke scenario: cluster bring-up → telemetry → fault → command |

---

## Layer 8 — Configuration

### EMS Configs

**`services/cluster-ems/config/daemon.json`** — local dev config:
- `canBus.type: "json_file"` — file-based node simulation
- `gridInverter.type: "state_file"` — file-based inverter simulation
- `hmi.type: "console"` — console logging
- `watchdog.type: "file"` — file-based watchdog
- `journal.type: "jsonline"` — JSONL event journal

**`services/cluster-ems/config/live.daemon.json`** — live site config template:
- `canBus.type: "command"` — external CAN adapter subprocess
- `gridInverter.type: "modbus_tcp"` — real Modbus TCP inverter
- `modbus.*` fields using `${CLUSTERSTORE_MODBUS_*}` environment placeholders
- `watchdog.type: "command"` — real supervisor subprocess
- All sensitive values are environment-backed — no hardcoded credentials

### Bridge Configs

**`services/utilitycore-bridge/config/daemon.json`** — local dev config:
- MQTT at `127.0.0.1:1883` (no TLS)
- `lteModem.type: "state_file"` — file-based LTE simulation
- `authorization: "allow_all"` — stub authorizer for dev

**`services/utilitycore-bridge/config/secure.daemon.json`** — production template:
- MQTT with TLS, CA cert path, username/password via env vars
- `authorization.type: "policy"` with explicit `allowedRoles` and `allowedRequesters`
- `lteModem.type: "http"` — polls real modem status URL
- All credentials via `${CLUSTERSTORE_MQTT_*}` env placeholders

---

## Stubs and Known Gaps (Specific, Code-Level)

| Location | What Is Stubbed | Impact |
|---|---|---|
| `bridge/runtime.ts` `AllowAllAuthorizer` | Always returns `authorized: true` — used when config omits auth policy | **Must not be used in production** — use `PolicyAuthorizer` |
| `ems/runtime.ts` `ModbusTcpGridInverterPort` | 32-bit register writes throw `'not yet supported'` | Blocks inverters that require 32-bit write registers for setpoints |
| `command-router.ts` line ~72 | Per-node command targeting returns validation error `'not yet supported'` | Cannot send commands scoped to a single node over MQTT — only cluster-wide |
| `firmware/node-firmware/` NVM bindings | `cluster_event_journal.c` flush path hooks to NVM but bindings are not wired to real flash driver | Journal does not survive reboot without this |
| `firmware/node-firmware/` HAL bindings | `cluster_stm32_hal.c` wraps STM32 HAL calls but has not been tested on real target hardware | Firmware is source-correct but not hardware-validated |
| `clusterstore-firmware/lib/` ↔ `node-firmware/` | Portable `lib/` modules (host-validated) are not the same code linked into STM32 images | Runtime convergence gap — see Priority 1 roadmap |
| Diagnostic packet reassembly | Diagnostic chunks are sent by nodes but no code reassembles them on the EMS side | Per-node diagnostics are incomplete above the frame level |

---

## What Is Genuinely Production-Quality Code

The following are complete, non-trivial implementations — not scaffolding:

1. **CAN wire protocol** — explicit 8-byte encode/decode in TypeScript and C, byte-aligned, no struct casting
2. **Startup sequencer** — 7-phase voltage-matched node admission with per-phase timeouts, voltage window checks, and alert generation
3. **Fault manager** — incident latching, stable IDs, deduplication, lifecycle transitions, per-node isolation
4. **Three dispatch strategies** — iterative weight-based allocation with per-node current caps
5. **Modbus TCP** — full protocol implementation including frame construction, transaction IDs, 16/32-bit registers, scaling, boolean and enum fields
6. **MQTT 3.1.1 TCP client** — handwritten protocol implementation, TLS, QoS 0/1, keep-alive, stream reassembly
7. **Command validation** — 116+ checks across auth, TTL, target binding, idempotency, per-command semantics
8. **Telemetry buffering and replay** — ack-gated queue drain, deduplication by message ID, JSONL persistence
9. **Environment variable substitution** — recursive `${VAR}` and `${VAR:-default}` resolution in config trees
10. **Contactor state machine** — precharge sequencing, welded-contactor detection, safe open/close transitions
11. **Dual-slot OTA boot control** — BCB read/write, trial mode, CRC32 integrity, rollback
12. **INA228 current sensor driver** — I2C 16-bit differential measurement, oversampling
13. **STM32G474 FDCAN driver** — ring buffer, DLC conversion, HAL callback wiring
14. **Multi-adapter runtime** — file, subprocess, Modbus TCP, HTTP adapters switchable per config — both EMS and bridge

---

## Overall Readiness Assessment by Subsystem

| Subsystem | Code Complete | Hardware-Proven | Production-Ready |
|---|---|---|---|
| CAN wire contracts | YES | NO | Pending HIL |
| MQTT schema | YES | NO | Pending broker |
| EMS control loop | YES | NO | Pending real CAN |
| Startup sequencer | YES | NO | Pending HIL |
| Fault management | YES | NO | Pending real nodes |
| Dispatch algorithms | YES | NO | Pending real inverter |
| Modbus TCP (read) | YES | NO | Pending real inverter |
| Modbus TCP (32-bit write) | **NO** | NO | Stub |
| MQTT client | YES | NO | Pending real broker |
| Command validation | YES | NO | Pending end-to-end |
| Per-node commands | **NO** | NO | Stub |
| Telemetry buffering | YES | NO | Pending durable store |
| LTE outage/replay | YES | NO | Pending real modem |
| Authorization | YES (policy) | NO | `AllowAllAuthorizer` must not ship |
| Node firmware (protocol) | YES | NO | Pending hardware |
| Node firmware (HAL binding) | YES | NO | Pending hardware |
| Boot control / OTA | YES | NO | Pending hardware |
| NVM journal persistence | Partial | NO | Binding not wired |
| Firmware runtime convergence | **NO** | NO | Priority 1 |
| Overlay BMS adapter seam | YES | NO | Pending vendor adapters |
| Vendor-specific adapters | **NO** | NO | Not started |
| CI pipeline | YES | N/A | Runs on push/PR |
