# ClusterStore — Complete Project Overview

**Date:** 2026-04-30  
**Status:** Architecture-complete, pre-pilot — strong software foundation, hardware binding and production hardening still outstanding.

---

## 1. What Is ClusterStore

ClusterStore is a software-defined control and operations platform for clustered battery energy storage systems (BESS). It provides the coordination, supervision, telemetry, and remote-management layer that sits between individual battery nodes and a cloud-connected fleet operations platform called UtilityCore.

The core idea is straightforward: individual storage nodes — whether custom-built PowerHive hardware or existing commercial BESS units from vendors like Victron, Pylontech, Growatt, or Deye — become members of a managed cluster. ClusterStore orchestrates them as a unified energy asset: equalizing charge state, dispatching current, isolating faults, reporting upward to a cloud dashboard, and accepting remote control commands with appropriate safety guardrails.

ClusterStore is not a BMS. Each node retains its own local BMS protection. ClusterStore is the coordination intelligence above that.

---

## 2. The Problem It Solves

### Without ClusterStore

Multiple BESS units deployed at a site operate independently. Each enforces its own protection limits but there is no shared bus coordination, no startup equalization, no synchronized dispatch, and no unified telemetry feed. Connecting mismatched-SoC nodes to a shared DC bus risks high circulating currents. Faults on one node can cascade. Operations teams have no fleet-level visibility without vendor-specific tooling per brand.

### With ClusterStore

- Nodes are admitted to the cluster in a controlled sequence with precharge and SoC equalization before shared bus connection.
- Current is allocated across nodes according to configurable dispatch strategies (equal-current, SoC-weighted, temperature-weighted).
- Faulty nodes are isolated without dropping the cluster — remaining nodes take over in degraded mode.
- The full cluster looks like a single energy asset to UtilityCore, regardless of how many nodes are underneath or which vendor manufactured them.
- Remote operators can send force-charge, force-discharge, and isolation commands through a secured, idempotent, acknowledged MQTT command path.
- Real-time telemetry and fault alerts flow to cloud dashboards with store-and-forward resilience through LTE outages.

---

## 3. Application and Use Cases

### 3.1 Commercial and Industrial Energy Storage

A site with grid-connected solar and storage uses ClusterStore to manage battery dispatch in response to grid tariffs. The EMS receives inverter readings over Modbus, calculates available charge headroom, and dispatches current to nodes at the optimal rate. UtilityCore operators can send override commands (force charge, force discharge) with expiry times and ClusterStore applies them safely within local protection limits.

### 3.2 Off-Grid and Hybrid Sites

Remote sites with diesel generators or solar-hybrid setups use ClusterStore to maximize battery utilization while protecting cells. The supervision timeout fail-safe ensures nodes return to standalone-safe behavior if the cluster controller loses power or connectivity — the system degrades gracefully rather than tripping.

### 3.3 Modular BESS as a Service (MAAS / MaaS)

UtilityCore's fleet layer enables a Modular Battery-as-a-Service model. Multiple sites each run a ClusterStore stack. A central operations team manages all of them through a single UtilityCore dashboard: monitoring SoC curves, fault event histories, cycle analytics, and sending coordinated dispatch overrides across the fleet. ClusterStore makes each site addressable as a first-class asset in that model.

### 3.4 Overlay / Retrofit Deployments

Sites that already have Victron, Pylontech, Growatt, or Deye BESS units can participate in ClusterStore via the overlay adapter path without replacing hardware. The adapter normalizes existing vendor telemetry and command protocols into ClusterStore node semantics. The EMS, cloud bridge, and fleet platform see no difference between a native node and an overlay node.

### 3.5 Pilot and R&D Sites

Sites running custom PowerHive hardware with STM32G474 nodes use the native firmware path. ClusterStore owns the full embedded control stack on these nodes — CAN protocol, state machine, contactor sequencing, OTA, and event journal. This path is intended for PowerHive's own first-party hardware deployment.

---

## 4. System Architecture

### 4.1 Three Runtime Layers

```
┌─────────────────────────────────────────────────────────┐
│                  UtilityCore / MAAP                      │
│   Telemetry Pipeline · Fleet Control · Dashboards / MAAS │
└────────────────────────┬────────────────────────────────┘
                         │ MQTT over LTE/4G
┌────────────────────────▼────────────────────────────────┐
│             Cluster Controller (DIN Rail IPC)            │
│  ┌──────────────┐  ┌───────────┐  ┌──────────────────┐  │
│  │  Cluster EMS  │  │  LTE/MQTT │  │  Local HMI / LCD │  │
│  │  (Node JS TS) │◄►│  Bridge   │  │  (field display) │  │
│  └──────┬───────┘  └───────────┘  └──────────────────┘  │
│         │ CAN 500 kbps         Modbus RTU/TCP            │
│  ┌──────▼───────────────────────────────────────────┐    │
│  │  Grid-side Inverter (Modbus) / Site Meter        │    │
│  └──────────────────────────────────────────────────┘    │
└────────────────────────┬────────────────────────────────┘
                         │ CAN bus (500 kbps)
┌────────────────────────▼────────────────────────────────┐
│                    Node Edge Layer                        │
│  ┌──────────────────┐   ┌──────────────────────────────┐ │
│  │  Native Node      │   │  Overlay Node Adapter        │ │
│  │  STM32G474RET6    │   │  Victron / Pylontech /       │ │
│  │  (ClusterStore FW)│   │  Growatt / Deye (Modbus/CAN) │ │
│  └──────────────────┘   └──────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

**Layer 1 — Node edge:** Individual BESS nodes. Each protects its own cells and exposes ClusterStore telemetry and command semantics upward via CAN. Can be either native STM32G474 hardware or an overlay adapter wrapping existing third-party BESS.

**Layer 2 — Cluster controller:** The coordination brain. Runs the Cluster EMS (Node.js/TypeScript) on a DIN rail industrial PC. Polls nodes over CAN, reads the inverter over Modbus, runs startup equalization and dispatch, isolates faults, kicks the hardware watchdog, and passes telemetry upward through the bridge.

**Layer 3 — UtilityCore / MAAP:** The cloud fleet platform. Receives structured telemetry via MQTT, surfaces dashboards, and issues remote dispatch commands back down to site controllers.

### 4.2 Internal Services and Packages

| Component | Location | Role |
|---|---|---|
| `cluster-ems` | `services/cluster-ems/` | Core EMS: startup, dispatch, fault management, watchdog, Modbus, CAN |
| `utilitycore-bridge` | `services/utilitycore-bridge/` | MQTT telemetry publish, command intake, SCADA fanout, LTE resilience |
| `contracts` | `packages/contracts/` | Shared TypeScript types for CAN frames, MQTT envelopes, telemetry, commands |
| `clusterstore-firmware` | `firmware/clusterstore-firmware/` | Portable C firmware: boot control, flash journal, CAN protocol, platform vtable |
| `node-firmware` | `firmware/node-firmware/` | Node-level runtime interfaces: state machine, contactor, OTA, CAN definitions |
| `clusterstore-can-adapter.mjs` | `scripts/` | CLI shim for EMS ↔ CAN bus file exchange (statuses, commands, diagnostics) |

---

## 5. Node Deployment Modes

### Mode A — Native Node (STM32G474)

ClusterStore owns the entire embedded stack on the node hardware.

- **MCU:** STM32G474RET6
- **CAN bus:** FDCAN1 at 500 kbps (classical CAN for MVP)
- **Watchdog:** IWDG (independent hardware watchdog)
- **Flash layout:** Dual-slot OTA — bootloader + slot A + slot B + event journal
- **OTA:** CRC32-validated image with trial/confirm/rollback state machine
- **Power measurement:** INA228 current and power monitor
- **Firmware modules:** `cluster_platform`, `cluster_can_protocol`, `cluster_flash_layout`, `cluster_boot_control`, `cluster_crc32`, `cluster_contactor_manager`, `cluster_current_ramp`, `cluster_ota_manager`, `cluster_node_controller`, `cluster_event_journal`, `cluster_state_machine`

Each native node:
- Sends `NODE_STATUS` frames (SoC, voltage, current, temperature, fault flags, heartbeat age) on its CAN heartbeat cycle
- Receives `NODE_CMD` frames (mode, charge/discharge setpoint, contactor command, supervision timeout)
- Enforces EMS commands only within local BMS limits
- Returns to standalone-safe behavior if EMS heartbeat is lost (supervision timeout)
- Performs contactor precharge sequence before closing the main contactor
- Detects welded contactors and latches a fault

### Mode B — Overlay Node Adapter

ClusterStore supervises an existing BESS asset through its native protocol. No embedded firmware change is needed on the asset.

Supported asset types:
- **Victron** (Venus OS / Modbus TCP)
- **Pylontech** (CAN-BMS or RS485)
- **Growatt** (Modbus RTU)
- **Deye** (Modbus RTU/TCP)

The overlay adapter (`bms-adapter.ts`) translates vendor telemetry into `NodeStatusFrame` and `NodeDiagnosticFrame` objects identical to what a native node would produce. The EMS layer above cannot distinguish between the two.

**Convergence:** Both modes produce the same `NodeStatusFrame` and accept the same `NodeCommandFrame`. Dispatch, telemetry, fault isolation, and cloud reporting are mode-agnostic.

---

## 6. Cluster EMS — Detailed Operation

### 6.1 Startup Sequencer (6-Phase State Machine)

When the EMS boots or restarts it does not immediately put nodes on the shared bus. It runs a controlled admission sequence:

| Phase | What Happens |
|---|---|
| `discover_nodes` | Poll CAN for live nodes; collect initial SoC and fault state |
| `precharge_primary` | Drive the precharge circuit; wait for bus voltage to ramp safely |
| `close_primary` | Close the primary contactor; confirm feedback |
| `admit_nodes` | Admit nodes one-by-one in voltage order; check voltage window after each admission |
| `balance_cluster` | Run balancing until SoC spread falls within configured threshold |
| `ready` / `failed` | Transition to normal dispatch loop, or latch a startup failure |

Each phase has a configurable timeout. If a phase times out, the sequencer fails safely and prevents dispatch. The `phaseEnteredAtMs` field is guarded against NaN/undefined so elapsed time calculations never produce garbage.

### 6.2 Dispatch Allocation

Once running, the EMS runs a control cycle on a configurable tick rate. Each cycle:

1. Reads node status from CAN (or overlay adapter)
2. Reads inverter state (Modbus) — available charge headroom, grid power
3. Runs the dispatch allocator: filters healthy nodes, computes per-node current allocations
4. Emits `NODE_CMD` frames to all nodes
5. Rolls telemetry into the bridge pipeline

**Dispatch strategies:**

| Strategy | Behavior |
|---|---|
| `equal_current` | Divides available current equally across all healthy nodes |
| `soc_weighted` | Charges nodes with lower SoC more; discharges nodes with higher SoC more |
| `temperature_weighted` | Favors cooler nodes for higher current; protects hot nodes |

The allocator uses a weighted fair-share while loop, caps each node at `maxCurrentPerNodeA`, and distributes any post-loop remainder to the least-loaded uncapped node. Nodes with fault flags, maintenance lockout, service lockout, or a stale heartbeat are excluded from dispatch but kept alive in standby.

### 6.3 Fault Management

The fault manager receives fault events from the node telemetry and site metering paths.

- **Latched incidents:** Each unique fault gets one incident ID. Subsequent cycles with the same fault do not create new IDs.
- **Deduplication:** The same fault across multiple cycles updates the existing incident rather than spamming new records.
- **Cap:** The active incident map is capped at 256 entries to prevent memory growth in runaway fault scenarios.
- **Operator acknowledgement:** Incidents can be acknowledged; cleared faults auto-resolve.
- **Node isolation:** When a node's fault flags are set and the EMS cannot clear them, it sends a `cluster_isolated` command and recalculates dispatch over the remaining healthy nodes.
- **Degraded mode:** The cluster continues operating at reduced capacity with isolated nodes removed from dispatch.

### 6.4 Remote Command Path

Operators send commands through UtilityCore. Each command travels:

```
UtilityCore → MQTT → Bridge → Command Router → EMS → Node CAN
```

Security and safety checks at each layer:

| Check | Where |
|---|---|
| Target site/cluster validation | Bridge command router |
| TTL / expiry check | Bridge command router |
| Auth metadata validation | Bridge command router |
| Idempotency deduplication | Bridge (`commandLedger.hasSeen()`) |
| `currentA` finite + positive | Bridge + EMS |
| EMS-side safety validation | `ems-controller.ts` `validateRemoteCommand()` |
| Per-node limit enforcement | Node firmware (local BMS) |

Rejected commands are journalled with the rejection reason. Each command generates a lifecycle of acknowledgement messages: `accepted`, `completed`, or `rejected`.

**Supported remote commands:**

| Command | Effect |
|---|---|
| `force_charge` | Override normal dispatch; force charge at specified current (A) with expiry |
| `force_discharge` | Override normal dispatch; force discharge at specified current (A) with expiry |
| `isolate_node` | Remove a specific node from cluster operation |
| `maintenance_lockout` | Lock a node out of dispatch for maintenance |
| `clear_fault` | Acknowledge and attempt fault clearance on a node |

### 6.5 Watchdog and Fail-Safe

The EMS kicks a hardware watchdog on every healthy control cycle. If the cycle throws an unhandled exception:

- The fail-safe path is triggered
- The EMS journals an `ems.fail_safe` event
- The watchdog is no longer kicked; the hardware supervisor reboots the controller
- All nodes independently time out their supervision window and return to standalone-safe mode

### 6.6 Modbus Inverter Integration

The EMS reads the grid-side inverter and site meter over Modbus RTU or Modbus TCP. The runtime reads numeric register words, applies scale factors, and produces structured inverter state (grid voltage, grid frequency, available charge headroom, AC output power). Modbus exception responses are decoded to human-readable names (Illegal Function, Illegal Data Address, Server Device Failure, etc.) for diagnostics.

---

## 7. UtilityCore Bridge — Detailed Operation

### 7.1 Telemetry Pipeline

Every EMS control cycle produces a `ClusterTelemetrySnapshot`:
- Aggregate SoC, power, energy
- Per-node SoC, voltage, current, temperature, fault flags, heartbeat age
- Site metering data (preferred over inferred node data when available)
- Cluster mode and health

The bridge publishes this snapshot to UtilityCore via MQTT. Each snapshot is tagged with a unique `id` field to support deduplication in the buffer.

### 7.2 LTE Resilience and Store-and-Forward

When the MQTT connection is unavailable (LTE outage):
- Snapshots are written to an in-memory telemetry buffer (keyed by message ID to prevent duplicates)
- On reconnect, buffered messages are replayed in order and only removed from the buffer after MQTT publish is confirmed (ack-based drain)
- Failed publishes during the replay are re-buffered rather than silently dropped

### 7.3 SCADA Fanout

In parallel with the MQTT uplink, the bridge exposes a local SCADA interface. This allows:
- Substation SCADA systems to read cluster state via Modbus TCP registers
- On-site HMI displays and PLC integrations to access live data without cloud connectivity

### 7.4 Alert Publishing

Fault incidents produce immediate alert messages to UtilityCore, separate from the regular 60-second telemetry cycle. Latched incident IDs prevent alert spam on reconnect — only the first occurrence triggers a new alert.

---

## 8. Data Contracts

All inter-component communication uses typed contracts defined in `packages/contracts/`. This ensures wire format consistency across TypeScript services and C firmware.

| Contract Type | Description |
|---|---|
| `NodeStatusFrame` | CAN status payload: SoC, voltage, current, temp, fault flags, mode, heartbeat |
| `NodeCommandFrame` | CAN command payload: mode, charge/discharge setpoints, contactor, balancing, timeout |
| `NodeDiagnosticFrame` | Extended diagnostics: cell voltages, temperatures, cycle count |
| `MqttEnvelope<T>` | Wrapper for all MQTT messages: site/cluster targeting, auth metadata, TTL |
| `RemoteCommand` | Command payload: type, currentA, nodeId, requestedBy, expiry |
| `CommandAck` | Acknowledgement payload: status (accepted/completed/rejected/duplicate), reason |
| `ClusterTelemetrySnapshot` | Full cluster snapshot published to UtilityCore |
| `DispatchAllocation` | Per-node current allocation result from the dispatch engine |

The CAN wire encoding uses explicit 8-byte frame packing with fixed field offsets — no raw struct casts. The same field layout is defined in both TypeScript and C to prevent firmware/service protocol drift.

---

## 9. Current Implementation State

### 9.1 What Is Complete

**Contracts and wire protocol:**
- Full shared TypeScript type definitions across CAN, MQTT, telemetry, and commands
- Explicit 8-byte CAN frame encoding/decoding in both TypeScript and C
- Wire format alignment verified between TS contracts and embedded C definitions

**Cluster EMS service:**
- Startup sequencer with all 6 phases, per-phase timeouts, and fail-out logic
- Weighted dispatch allocator with equal-current, SoC-weighted, and temperature-weighted strategies
- Fault manager with latched incidents, deduplication, 256-entry cap, and operator acknowledgement
- Remote command path with full validation stack (target, TTL, auth, idempotency, payload, safety)
- EMS-side rejection journalling for auditable command history
- Modbus inverter runtime with exception code decoding
- Hardware watchdog integration and fail-safe journalling
- BMS overlay adapter normalizing third-party BESS assets into EMS node semantics
- Local event journal
- HMI adapter stub

**UtilityCore bridge service:**
- MQTT telemetry publish with ack-based store-and-forward buffer
- Command router with full security and safety validation
- Idempotency ledger (deduplicates replayed commands by ID)
- SCADA fanout seam
- Alert publishing with fault ID latching

**Node firmware (portable C modules):**
- Platform abstraction vtable (`cluster_platform`)
- Explicit CAN frame encode/decode (`cluster_can_protocol`)
- Flash partition map (`cluster_flash_layout`)
- Boot control with dual-slot trial/confirm/rollback (`cluster_boot_control`)
- CRC32 image integrity (`cluster_crc32`)
- Contactor manager with precharge, welded-contactor detection (`cluster_contactor_manager`)
- Current ramp limiting (`cluster_current_ramp`)
- OTA state machine (`cluster_ota_manager`)
- Node controller integrating platform vtable (`cluster_node_controller`)
- Event journal with fixed flush path (`cluster_event_journal`)
- Node FSM (`cluster_state_machine`)
- Host-side build and test passing on Windows

**STM32G474 BSP and firmware workspace:**
- `firmware/clusterstore-firmware/` scaffold with CMake build system
- STM32Cube G4 driver tree imported
- ARM cross-compile via `arm-none-eabi-gcc` with bare-metal syscall stubs (no `nosys` noise)
- Build targets: CAN bench image, native node slot images, bootloader entry point
- `build-g474-hal.ps1` script produces deterministic ARM output

**Testing:**
- In-process test harness (Node.js, no subprocess dependency)
- 17 passing tests covering: startup sequencer phases, fault manager latch/dedupe, command idempotency, replay-failure buffering, watchdog fail-safe trigger, LTE replay ordering, duplicate idempotency key rejection
- End-to-end smoke simulator (`scripts/smoke-simulator.mjs`): cluster bring-up → telemetry → fault → command
- Full-stack smoke test (`npm run smoke:stack`): fake MQTT broker + EMS daemon + bridge daemon

**CI:**
- GitHub Actions workflow: `npm test` + `npm run sim:smoke` on every push/PR

### 9.2 What Is Still Outstanding

**Firmware:**
- Bind portable C modules to real STM32G474 HAL (FDCAN, GPIO contactors, INA228, ADC, IWDG)
- Converge `firmware/clusterstore-firmware/` and `firmware/node-firmware/` — currently the STM32 images still link through the older runtime path
- Build first live CAN bench with a NUCLEO-G474RE and USB-CAN adapter
- NVM persistence binding for boot control and event journal

**Overlay adapters:**
- Complete vendor-specific implementations for Victron, Pylontech, Growatt, Deye beyond the normalized adapter seam

**EMS production hardening:**
- Persistent node registry (survives controller reboot)
- Brownout / black-start recovery
- Grid-flap handling
- State persistence after reboot
- Full alarm lifecycle (acknowledge → clear workflow in UtilityCore)
- Per-site operational envelopes and configuration bundles
- Commissioning workflow and maintenance mode UI

**Bridge production hardening:**
- TLS mutual authentication and certificate lifecycle
- Secure device provisioning and bootstrap
- Durable outbound queue with disk-backed replay (currently in-memory only)
- End-to-end command audit log
- Local diagnostics API for field technicians
- Bridge health metrics

**Fleet platform (UtilityCore):**
- ClusterStore asset type definition in UtilityCore
- Dedicated dashboards (per-node SoC, aggregate power/energy curves)
- Incident workflow and alert escalation
- Historical analytics and degradation modelling
- Policy engine for tariff-aware and backup-aware dispatch

**Security:**
- MQTT credential injection at runtime (currently config-file based)
- Role-based access control for remote commands
- Signed OTA artifacts with rollback guarantee

**Validation:**
- Hardware-in-the-loop (HIL) test bench
- Field commissioning checklist and rollback playbook
- Billing-grade metering accuracy validation

---

## 10. How It Runs — Demo, MVP, and Production

### 10.1 Demo Mode (No Hardware)

Everything runs in software on any development machine. A fake MQTT broker is spun up in-process. The EMS and bridge daemons start against simulated CAN and Modbus adapters.

**Steps:**
```bash
npm install
npm run smoke:stack          # boots fake broker + EMS + bridge, verifies end-to-end path
npm run sim:smoke            # runs the full cluster bring-up → telemetry → fault → command scenario
npm test                     # 17 unit/integration tests, all in-process
```

**What you see:**
- EMS startup sequencer phases logged to stdout
- Simulated node telemetry flowing through dispatch
- Fault injection and node isolation logged
- Remote command sent and acknowledged end-to-end
- All 17 tests PASS / 0 FAIL

**Outputs produced:**
- Test results: pass/fail counts per test
- Smoke simulator: JSON telemetry snapshots, command ack objects, fault incident records
- Daemon logs: timestamped phase transitions, dispatch allocations, MQTT publish events

### 10.2 MVP / Pre-Pilot (Real Hardware, Local Network)

At least one cluster controller (DIN rail IPC or equivalent) with CAN interface and real nodes (native STM32G474 or overlay-adapted Victron/Pylontech/etc.), connected to an inverter over Modbus, and a local Mosquitto MQTT broker.

**Steps:**
1. Validate config: `npm run check:live-readiness`
2. Start local Mosquitto: `npm run mqtt:mosquitto:run`
3. Start EMS daemon: `npm run start:ems` (with live CAN adapter config pointing to real CAN device)
4. Start bridge daemon: `npm run start:bridge` (with real MQTT broker address)
5. Probe live endpoints: `node scripts/live-readiness-check.mjs --ems <config> --bridge <config> --probe`
6. Flash STM32G474 nodes: `powershell firmware/clusterstore-firmware/scripts/build-g474-hal.ps1` → flash via SWD

**What you see:**
- EMS logs: node discovery on CAN, startup equalization phase-by-phase, dispatch current allocations per node
- Bridge logs: MQTT connection confirmed, telemetry snapshots published, commands received and routed
- Local MQTT topic `cluster/{siteId}/telemetry`: live JSON telemetry snapshots at configured interval
- Local MQTT topic `cluster/{siteId}/alerts`: fault incident records as they occur
- CAN bus traffic: `NODE_STATUS` frames from nodes, `NODE_CMD` frames from EMS

**Config files used:**
- `services/cluster-ems/config/example.live.daemon.json` — live CAN and Modbus paths
- `services/cluster-ems/config/example.can-adapter.json` — CAN file exchange paths
- `services/cluster-ems/config/example.watchdog-adapter.json` — hardware watchdog device
- `services/utilitycore-bridge/config/example.secure.daemon.json` — TLS MQTT credentials

**Outputs produced:**
- Live MQTT telemetry stream (SoC, power, per-node health every 60 seconds)
- Immediate alert MQTT messages on fault events
- Local event journal records (startup, dispatch, faults, commands)
- CAN command frames to node hardware

### 10.3 Production / Fleet (Full Deployment)

Multiple sites each running a ClusterStore stack, connected to UtilityCore cloud platform. Remote operators manage the fleet from a central dashboard. Sites run 24/7 with LTE uplink.

**Expected deployment per site:**
- DIN rail IPC with CAN interface, RS485/USB for Modbus, LTE modem
- One or more BESS nodes (native or overlay)
- Grid-side inverter with Modbus profile configured
- Site meter for billing-grade reconciliation
- Local HMI display for field technicians

**Expected fleet outputs:**
- UtilityCore dashboard: per-site SoC curves, aggregate power, fault history, cycle analytics
- MQTT telemetry stream to UtilityCore telemetry pipeline: 60-second snapshots per site
- Alert feed: immediate notifications on node faults, EMS fail-safe triggers, LTE loss
- Command audit trail: every remote command with requestedBy, timestamp, result, and rejection reasons
- OTA update delivery to node firmware via dual-slot mechanism with auto-rollback on failed boot

**Operational steady state per cycle:**
1. EMS ticks on configured interval (e.g. 1 second)
2. Reads all node statuses from CAN
3. Reads inverter and site metering over Modbus
4. Runs dispatch allocation
5. Emits `NODE_CMD` to all nodes
6. Publishes telemetry snapshot to bridge
7. Kicks hardware watchdog
8. Bridge publishes to MQTT (or buffers if LTE is down)
9. Bridge drains buffer on reconnect in order with ack confirmation

---

## 11. External Integrations

| Integration | Protocol | Direction | Status |
|---|---|---|---|
| Battery nodes (native) | CAN 500 kbps, FDCAN1 | Bidirectional | Firmware scaffold complete; HAL binding outstanding |
| Battery nodes (overlay) | Modbus RTU / CAN-BMS | Bidirectional | Adapter seam complete; vendor-specific implementations outstanding |
| Grid inverter | Modbus RTU or TCP | Read (primarily) | Runtime complete; real inverter profile needs commissioning |
| Site meter | Modbus TCP | Read | Seam exists; billing-grade calibration outstanding |
| UtilityCore cloud | MQTT over TLS/LTE | Bidirectional | Logic complete; TLS credential lifecycle outstanding |
| Local SCADA | Modbus TCP/RTU | Read (SCADA reads cluster state) | Fanout seam complete; register map not finalized |
| Local HMI | Internal API | Write | Adapter stub present; display not implemented |
| Hardware watchdog | OS device or GPIO | Write (kick) | Adapter complete; device path site-specific |
| LTE modem | OS network interface | Underlying transport | Connectivity used; modem health monitoring not built |
| OTA delivery | MQTT or direct file | Write to node | OTA state machine complete in firmware; delivery pipeline not built |

---

## 12. Repository Layout

```
clusterStore/
├── docs/
│   ├── architecture.md              System context and control flows
│   ├── clusterstore-master-plan.md  Full planning reference, phases, exit criteria
│   ├── node-deployment-modes.md     Mode A vs Mode B detail
│   ├── audit-and-runbook.md         Operations runbook and deployment guide
│   └── project-overview.md          (this document)
├── firmware/
│   ├── clusterstore-firmware/       Portable C firmware workspace (STM32G474, host-tested)
│   └── node-firmware/               Node-level runtime interfaces and state machine
├── packages/
│   └── contracts/                   Shared TS types (CAN, MQTT, telemetry, commands)
├── services/
│   ├── cluster-ems/                 EMS orchestration service
│   │   ├── src/
│   │   │   ├── startup-sequencer.ts
│   │   │   ├── ems-controller.ts
│   │   │   ├── fault-manager.ts
│   │   │   ├── dispatch.ts
│   │   │   ├── runtime.ts           (Modbus runtime)
│   │   │   └── adapters/
│   │   │       └── bms-adapter.ts   Overlay asset normalization
│   │   └── config/                  Example daemon configs
│   └── utilitycore-bridge/          MQTT bridge service
│       ├── src/
│       │   ├── bridge-service.ts
│       │   └── command-router.ts
│       └── config/                  Example bridge configs
├── scripts/
│   ├── clusterstore-can-adapter.mjs CLI for CAN file exchange
│   ├── smoke-simulator.mjs          End-to-end scenario simulator
│   └── live-readiness-check.mjs     Live endpoint probe
└── tests/
    └── all.test.mjs                 17-test in-process harness
```

---

## 13. Delivery Roadmap

### Phase 0 — Foundation (Complete)

- Shared contracts and wire protocol
- EMS orchestration logic (startup, dispatch, faults, commands)
- UtilityCore bridge (MQTT, store-and-forward, command routing)
- Portable firmware modules (C, host-validated)
- STM32G474 firmware workspace and ARM build
- In-process test harness and CI

### Phase 1 — First Hardware Validation (Next)

- Bind portable firmware to real STM32G474 HAL (FDCAN, GPIO, INA228, IWDG)
- Build CAN bench with NUCLEO-G474RE
- Converge firmware runtimes
- First real inverter Modbus profile
- Local Mosquitto integration test with real MQTT
- HIL bench for key failure modes (supervision timeout, contactor fault, LTE drop)

### Phase 2 — Pilot Readiness

- Full commissioning workflow
- Persistent controller state (survives reboot)
- TLS mutual auth and credential provisioning
- Durable telemetry queue (disk-backed)
- Overlay adapters for first commercial vendor (Pylontech or Victron)
- Alarm acknowledge/clear lifecycle

### Phase 3 — Commercial Rollout

- OTA pipeline with signed artifacts
- Configuration management and staged rollout
- UtilityCore ClusterStore asset model and dashboards
- Multi-site fleet monitoring
- Billing-grade metering integration
- Field service tooling

### Phase 4 — Differentiation

- Predictive fault detection and degradation modelling
- Tariff-aware and backup-aware dispatch policy engine
- Self-healing cluster control (dynamic node admission/eviction)
- Distributed ledger for energy accountability (MaaS billing)
- Multi-inverter support per site

---

## 14. Pilot Exit Criteria

A site is ready for first pilot energization when:

1. CAN wire protocol is frozen and verified on real hardware
2. Modbus profile is validated against the target inverter
3. Startup equalization and contactor sequencing run correctly on hardware
4. Safety supervisor and watchdog operate independently of software crashes
5. Durable telemetry and event journal survive controller power cycles
6. Remote command path is secured, auditable, and rejection-journalled
7. Commissioning workflow and maintenance lockout are exercised
8. Alarm latch, acknowledge, and clear lifecycle is complete
9. Local HMI provides sufficient field diagnostics without cloud connectivity
10. HIL simulation covers: node fault, LTE drop, EMS crash, OTA rollback

---

## 15. Commercial Rollout Exit Criteria

Ready for broad deployment when:

1. OTA with signed artifacts and rollback proven across 5+ sites
2. Configuration management allows per-site parameter bundles without code deploys
3. Fleet health monitoring covers all sites in UtilityCore
4. Predictive analytics identifies battery degradation patterns
5. Multi-inverter Modbus profiles validated (at minimum: SMA, Sungrow, or equivalent)
6. Field service tooling enables technician access without engineering involvement
7. Security baseline meets industrial deployment standards (IEC 62443 or equivalent)
8. UtilityCore SLA operations workflows (incident response, escalation, audit)

---

## 16. Key Design Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Intra-cluster bus | CAN 500 kbps | Deterministic latency, proven in automotive/industrial, simple cabling |
| Node MCU | STM32G474RET6 | FDCAN peripheral, floating-point, sufficient flash/RAM, known supply chain |
| Inverter protocol | Modbus RTU/TCP | Universal in grid-tie inverters; no proprietary dependency |
| Cloud uplink | MQTT over LTE | Low-bandwidth telemetry, inherent pub/sub for fan-out, standard across energy IoT |
| EMS runtime | Node.js / TypeScript | Rapid iteration, strong type system for contracts, runs on Linux IPC hardware |
| Firmware language | C (portable, no RTOS) | Bare-metal reliability, no scheduler overhead, host-testable with vtable abstraction |
| Dispatch algorithm | Weighted fair-share | Adaptable to SoC, temperature, and equal strategies without algorithm swap |
| OTA strategy | Dual-slot with CRC32 | Atomic update with guaranteed rollback; no half-flashed bricks |
| Telemetry resilience | In-memory ack-based buffer | Survive LTE drops; drain in order on reconnect |
| Command safety | Multi-layer validation | Bridge → router → EMS → firmware; each layer independently enforces its constraints |

---

## 17. What ClusterStore Is Not

- **Not a BMS.** Each node's BMS remains its own concern. ClusterStore cannot override local cell protection limits.
- **Not a grid inverter controller.** ClusterStore reads inverter state and sends current setpoints through the EMS; it does not replace inverter firmware.
- **Not a metering system.** ClusterStore uses meter data for dispatch decisions; billing-grade reconciliation requires a calibrated meter integration that is not yet built.
- **Not a UtilityCore replacement.** The bridge connects to UtilityCore; the fleet platform itself (dashboards, policy engine, billing) is an external dependency.
- **Not yet production-hardened.** The software architecture is complete and coherent. The remaining work is hardware binding, security provisioning, durable persistence, and commissioning tooling.
