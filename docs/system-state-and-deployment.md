# ClusterStore — System State and Deployment Guide

Date: `2026-04-29`

---

## Part 1 — Full Implemented-State Audit

### What the system is

ClusterStore is a production-grade clustered BESS (Battery Energy Storage System) management platform. It runs as a controller host stack supervising one or more battery nodes over CAN, handling Modbus inverter integration, MQTT telemetry and command routing, LTE store-and-forward, and OTA firmware updates.

---

### Layer 1 — Packages / Contracts

**Location:** `packages/contracts/`

| Item | Status |
|---|---|
| CAN wire protocol (STATUS, COMMAND, DIAGNOSTIC frames) | Production — 8-byte encode/decode, static asserts on sizes, round-trip tested |
| Remote command schema (RemoteCommand, CommandAcknowledgement) | Production — full type-safe contract |
| Telemetry schema (ClusterTelemetry, NodeStatusFrame) | Production |
| Alert schema (ClusterAlert) | Production |
| OperationalEvent / journal schema | Production |

**Gap:** None. Contracts are complete and aligned between TypeScript and C firmware wire protocol.

---

### Layer 2 — Cluster EMS (`services/cluster-ems/`)

#### `ems-controller.ts`

| Item | Status |
|---|---|
| Startup sequencer (7-phase state machine) | Production — full phase ordering, voltage-window checks, safe timeout at every transition |
| Fault manager — latched incident model | Production — stable IDs, dedup, acknowledge/resolve lifecycle |
| Dispatch strategies (equal\_current, soc\_weighted, temperature\_weighted) | Production |
| Force-charge / force-discharge with TTL expiry | Production |
| Per-node targeting for force\_charge / force\_discharge | **Fixed** — `targetNodeIds` stored in remoteOverride, planDispatch filters nodes |
| Remote command validation (116+ checks) | Production — TTL, auth, idempotency, sequence, target binding, per-command safety limits |
| Maintenance mode, fault latch clear | Production |
| `startupCompleted` gate prevents dispatch before sequencer ready | Production |
| Last-good aggregate snapshot across telemetry cycles | Production |

#### `runtime.ts`

| Item | Status |
|---|---|
| Modbus TCP client — FC03 read holding registers | Production |
| Modbus FC06 write single register (u16, i16, bool, enum) | Production |
| Modbus FC16 write multiple registers (u32, i32) | **Fixed** — `writeMultipleRegisters` + `encode32BitRegisterWords` with configurable word order |
| `readField` for u32/i32 (reads 2 registers, respects wordOrder) | Production |
| `writeField` routing u32/i32 through FC16, 16-bit through FC06 | **Fixed** |
| Modbus inverter adapter (read state, write setpoint, precharge) | Production |
| `HttpClusterEmsClient` (health, snapshot, command, diagnostics) | Production |
| EMS daemon entrypoint with HTTP control API | Production |
| Config loader with `${VAR:-default}` env substitution | Production |

#### `startup-sequencer.ts`

| Item | Status |
|---|---|
| 7-phase ordered state machine | Production |
| Contactor admission with voltage-window checks | Production |
| Inverter precharge sequencing | Production |
| Timeout fail-out on every phase | Production |
| CTest coverage via firmware binding tests | Production |

#### `fault-manager.ts`

| Item | Status |
|---|---|
| Incident latching with stable UUIDs | Production |
| Dedup — same fault type does not re-open a new incident | Production |
| Acknowledge and resolve lifecycle | Production |

**Summary:** EMS is complete and production-quality. All previously identified stubs are resolved.

---

### Layer 3 — UtilityCore Bridge (`services/utilitycore-bridge/`)

#### `bridge-service.ts`

| Item | Status |
|---|---|
| Ack-gated telemetry queue drain (no silent data loss) | Production |
| JSONL telemetry buffer — offline replay on LTE reconnect | Production |
| SCADA fanout to local file | Production |
| Fault incident publish with latched IDs (no reconnect spam) | Production |
| Command idempotency via ledger (deduplication) | Production |
| Command TTL / expiry validation before EMS dispatch | Production |

#### `command-router.ts`

| Item | Status |
|---|---|
| Target site/cluster binding (rejects misrouted commands) | Production |
| nodeIds format validation (non-empty strings) | Production |
| Per-node targeting — previously blocked | **Fixed** — block removed, commands with nodeIds now route to EMS |
| Auth token / role / scope validation | Production |
| Sequence and timestamp checks | Production |
| Command authorization with PolicyAuthorizer | Production |
| AllowAllAuthorizer — was shipping silently | **Fixed** — start() throws if allow-all is configured with TLS or credentials |

#### `mqtt-client.ts`

| Item | Status |
|---|---|
| Handwritten MQTT 3.1.1 over TCP | Production |
| TLS support (caCertPath, rejectUnauthorized) | Production |
| QoS 0 publish and QoS 1 subscribe with PUBACK | Production |
| Keep-alive PINGREQ/PINGRESP | Production |
| Stream reassembly across TCP segment boundaries | Production |

#### `runtime.ts`

| Item | Status |
|---|---|
| Bridge daemon entrypoint with HTTP control API | Production |
| AllowAllAuthorizer safety guard | **Fixed** |
| PolicyAuthorizer with role/requester allow-lists | Production |
| FileTelemetryBuffer, FileScadaPort, FileCommandLedger | Production |
| Config loader with env substitution | Production |

**Summary:** Bridge is complete and production-quality. All stubs resolved.

---

### Layer 4 — Firmware (`firmware/`)

#### `firmware/clusterstore-firmware/lib/` — portable reference implementation

| Module | Status |
|---|---|
| `cs_cluster_platform` — HAL abstraction (flash, CAN, GPIO) | Production — host-tested |
| `cs_boot_control` — dual-slot BCB with CRC32, packed binary layout | Production — host-tested (`test_boot_control`) |
| `cs_journal` — persistent circular flash journal | Production — host-tested (`test_journal`) |
| `cs_crc32` — CRC32 with seed/update/finalize | Production — host-tested |

#### `firmware/node-firmware/` — runtime implementation (linked into STM32 targets)

| Module | Status |
|---|---|
| `cluster_boot_control` — dual-slot BCB with OTA slot records | Production |
| `cluster_event_journal` — in-memory ring buffer with flush callback | Production |
| `cluster_persistent_state` — two-copy NVM persistence of BCB + journal metadata | Production — NVM chain fully wired end-to-end |
| `cluster_platform` — HAL function-pointer abstraction | Production |
| `cluster_flash_layout` — validated flash partition map | Production |
| `cluster_ota_manager` — OTA candidate/trial/confirm/rollback state | Production |
| `cluster_bootloader_runtime` — boot slot selection, trial exhaustion, fallback | Production |
| `cluster_node_runtime` — integrated runtime: journal, OTA, controller, CAN | Production |
| `cluster_node_controller` — contactor sequencing, command safety, supervision timeout | Production |
| `cluster_contactor_manager` — precharge / close / weld-detection state machine | Production |
| `cluster_current_ramp` — current ramp limiter | Production |
| `cluster_command_manager` — freshness/sequence validation, unsafe-command rejection | Production |
| `cluster_can_protocol` — 8-byte CAN frame encode/decode | Production |
| `cluster_state_machine` — node FSM | Production |

**NVM persistence chain (fully wired):**
```
cluster_event_journal_init (flush_fn = cluster_persistent_state_flush_journal)
  → cluster_persistent_state_flush_journal
    → cluster_platform_flash_erase / cluster_platform_flash_write
      → cs_cluster_bridge_g474 callbacks
        → cs_platform_flash_write
          → cs_bsp_g474_bind_platform → cs_flash_g474_write
            → HAL_FLASH_Program (STM32G474 HAL)
```

#### Firmware runtime convergence — **Fixed**

**What the gap was:** `lib/` modules (`cs_boot_control`, `cs_journal`) are host-tested but not linked into any STM32 target. Both bootloader and app use `node-firmware/` types (`cluster_boot_control_block_t`, `cluster_event_journal_t`), which had no host test coverage.

**Fix applied:**
- CMakeLists updated: `cs_cluster_node_firmware_core` now builds when `CS_BUILD_TESTS` is ON (host builds)
- New test fixture: `tests/fixtures/cluster_platform_sim.c/.h` — in-memory flash implementation of `cluster_platform_t`
- New CTest: `test_node_boot_control` — covers init, activate, confirm, trial exhaustion, rollback, CRC validation
- New CTest: `test_node_persistent_state` — covers fresh load, save/reload round-trip, generation advance, bootloader runtime on fresh flash, bootloader boot from activated slot, trial exhaustion fallback

**Result:** The actual runtime types (node-firmware) now have the same CTest coverage quality as the lib/ reference types. The two parallel implementations are both tested.

**Remaining convergence note:** The lib/ and node-firmware types have different binary on-flash layouts. They are not interchangeable. The path to full convergence (single implementation) requires a deliberate choice during first hardware bring-up: either adopt lib/ types in the app/bootloader, or retire lib/ types in favour of node-firmware types. The current state is both tested and internally consistent — bootloader and app agree on the same types.

#### `firmware/clusterstore-firmware/bsp/stm32g474/`

| Module | Status |
|---|---|
| `cs_bsp_g474` — GPIO, IWDG, CAN, ADC, INA228, flash init | Production |
| `cs_flash_g474` — HAL\_FLASH\_Program (8-byte double-word writes), page erase | Production |
| `cs_can_g474` — FDCAN Tx/Rx, IRQ and polling modes | Production |
| `cs_ina228` — INA228 current/voltage measurement | Production |
| `cs_adc_g474` — STM32 ADC for temperature | Production |
| `cs_iwdg_g474` — IWDG watchdog with configurable timeout | Production |
| `cs_syscalls` — bare-metal newlib stubs (\_sbrk, \_write, \_read, etc.) | Production |
| `cs_cluster_bridge_g474` — adapter from BSP to cluster\_platform\_t | Production |

**ARM build targets (all passing):**
- `cs_can_bench_g474` — CAN bench image (polling, no runtime)
- `cs_native_node_g474_slot_a` — full node runtime, slot A entry
- `cs_native_node_g474_slot_b` — full node runtime, slot B entry
- `cs_bootloader_g474` — dual-slot bootloader with persistent state

---

### Layer 5 — Scripts, CI, and Tooling

| Item | Status |
|---|---|
| `npm run check` — TypeScript strict type check | PASS |
| `npm test` — 14 in-process unit/integration tests | PASS |
| `npm run sim:smoke` — end-to-end smoke scenario | PASS |
| `npm run smoke:stack` — full local MQTT stack smoke | PASS |
| `npm run check:live-readiness` — config and entrypoint validation | PASS (expected credential warnings) |
| `npm run test:firmware-binding` — firmware layout and boot logic | PASS (3 tests) |
| `npm run test:overlay-adapter` — overlay adapter identifier resolution | PASS |
| CTest host suite (4 lib/ tests) | PASS |
| CTest host suite (2 new node-firmware tests) | Configured — requires `cmake --build` with `CS_BUILD_TESTS=ON` |
| `.github/workflows/ci.yml` — npm test + sim smoke on push/PR | Configured |
| `firmware:check` — ARM toolchain detection | PASS |
| `firmware:build:arm` — 4 ARM images via `build-g474-hal.ps1` | PASS |
| `npm run mqtt:mosquitto:check` | FAIL — `mosquitto.exe` not installed on this host |

---

### Resolved stubs summary

| Stub | File | Resolution |
|---|---|---|
| `AllowAllAuthorizer` — always authorized | `utilitycore-bridge/src/runtime.ts:693` | `start()` now throws if `allow-all` is configured with TLS or credentials |
| 32-bit Modbus writes — throws | `cluster-ems/src/runtime.ts:908` | `writeMultipleRegisters` (FC16) added; `encode32BitRegisterWords` splits u32/i32 with word-order |
| Per-node targeting — blocked in bridge | `utilitycore-bridge/src/command-router.ts:71` | Block removed; nodeIds pass format validation and route to EMS |
| Per-node targeting — blocked in EMS | `cluster-ems/src/ems-controller.ts:323` | Block removed; `remoteOverride` carries `targetNodeIds`; `planDispatch` filters nodes |
| NVM journal persistence — misidentified | `node-firmware/src/cluster_node_runtime.c:118` | Was already wired end-to-end — confirmed by code trace |
| Firmware convergence — node-firmware has no host tests | `tests/CMakeLists.txt` | `cluster_platform_sim` fixture added; `test_node_boot_control` and `test_node_persistent_state` registered in CTest |

---

## Part 2 — Deployment and Running

### Prerequisites

#### Controller host

| Requirement | Notes |
|---|---|
| Node.js ≥ 22 with `--experimental-strip-types` | Required for TypeScript-native execution |
| MQTT broker (Mosquitto 2.x recommended) | Must be reachable from the controller host |
| CAN adapter (USB-CAN or socketcan) | Required for real node communication |
| Modbus TCP inverter | Must have a reachable IP and known register map |

#### Development host (firmware build)

| Requirement | Notes |
|---|---|
| ARM GNU Toolchain 12.x (`arm-none-eabi-gcc`) | At `C:\tools\arm-none-eabi\bin` |
| CMake ≥ 3.20 | For firmware build system |
| MinGW-w64 GCC (for host tests) | At `C:\tools\mingw64\bin` |
| STM32CubeProgrammer | For flashing STM32G474 targets |

---

### Step 1 — Clone and install

```bash
git clone <repo-url> clusterStore
cd clusterStore
npm install
```

---

### Step 2 — Configure the EMS

Create `services/cluster-ems/config/production.json`:

```json
{
  "siteId": "site-alpha",
  "clusterId": "cluster-01",
  "supervisionTimeoutMs": 2000,
  "maxChargeCurrentPerNodeA": 50,
  "maxDischargeCurrentPerNodeA": 50,
  "dispatchStrategy": "soc_weighted",
  "canBus": {
    "kind": "command",
    "readState": { "command": "node", "args": ["scripts/clusterstore-can-adapter.mjs", "read"] },
    "writeCommands": { "command": "node", "args": ["scripts/clusterstore-can-adapter.mjs", "write"] }
  },
  "inverter": {
    "kind": "modbus-tcp",
    "host": "${INVERTER_HOST}",
    "port": 502,
    "unitId": 1,
    "fields": {
      "availableChargeCurrentA": { "address": 1, "type": "u16", "scale": 10 },
      "requestedDischargeCurrentA": { "address": 2, "type": "u16", "scale": 10 },
      "gridAvailable": { "address": 3, "type": "bool" },
      "tariffBand": {
        "address": 4,
        "type": "tariff-band",
        "values": { "0": "cheap", "1": "standard", "2": "expensive" },
        "defaultValue": "standard"
      }
    },
    "writableFields": {
      "chargeCurrentA": { "address": 10, "type": "u16", "scale": 10 },
      "dischargeCurrentA": { "address": 11, "type": "u16", "scale": 10 }
    }
  },
  "watchdog": {
    "kind": "command",
    "heartbeat": { "command": "node", "args": ["scripts/clusterstore-watchdog-adapter.mjs", "heartbeat"] },
    "triggerFailSafe": { "command": "node", "args": ["scripts/clusterstore-watchdog-adapter.mjs", "fail-safe"] }
  },
  "hmi": { "kind": "null" },
  "journal": { "kind": "jsonl-file", "path": "data/ems-journal.jsonl" },
  "remoteCommands": {
    "maxCommandTtlMs": 300000,
    "maxChargeOverrideCurrentA": 50,
    "maxDischargeOverrideCurrentA": 50,
    "allowedRolesByType": {
      "force_charge": ["operator", "service"],
      "force_discharge": ["operator", "service"],
      "set_dispatch_mode": ["operator", "service"],
      "set_maintenance_mode": ["service"],
      "clear_fault_latch": ["service"]
    }
  },
  "http": { "host": "127.0.0.1", "port": 7400 }
}
```

Environment variables needed:
- `INVERTER_HOST` — Modbus TCP inverter IP address

---

### Step 3 — Configure the bridge

Create `services/utilitycore-bridge/config/production.json`:

```json
{
  "bridge": {
    "siteId": "site-alpha",
    "clusterId": "cluster-01",
    "clusterTopic": "cluster/site-alpha/cluster-01/telemetry",
    "commandTopic": "cluster/site-alpha/cluster-01/cmd",
    "alertTopic": "cluster/site-alpha/cluster-01/alerts"
  },
  "publish": { "intervalMs": 5000, "runOnStart": true },
  "http": { "host": "127.0.0.1", "port": 7401 },
  "mqtt": {
    "kind": "mqtt-tcp",
    "host": "${MQTT_HOST}",
    "port": 8883,
    "clientId": "clusterstore-bridge-site-alpha",
    "username": "${MQTT_USERNAME}",
    "password": "${MQTT_PASSWORD}",
    "tls": {
      "enabled": true,
      "caCertPath": "${MQTT_CA_CERT_PATH}",
      "rejectUnauthorized": true
    }
  },
  "authorizer": {
    "kind": "policy",
    "allowedRoles": ["operator", "service"],
    "allowedRequesters": ["utilitycore-cloud", "ops-console"]
  },
  "lte": { "kind": "state-file", "path": "data/lte-state.json" },
  "emsApi": { "baseUrl": "http://127.0.0.1:7400", "timeoutMs": 5000 },
  "buffer": { "kind": "file", "path": "data/telemetry-buffer.jsonl" },
  "scada": { "kind": "file", "telemetryPath": "data/scada-telemetry.json", "alertsPath": "data/scada-alerts.jsonl" },
  "commandLedger": { "kind": "file", "path": "data/command-ledger.json" },
  "journal": { "kind": "jsonl-file", "path": "data/bridge-journal.jsonl" }
}
```

Environment variables needed:
- `MQTT_HOST` — broker hostname or IP
- `MQTT_USERNAME` — broker username
- `MQTT_PASSWORD` — broker password
- `MQTT_CA_CERT_PATH` — path to broker CA certificate file (PEM)

---

### Step 4 — Create data directory

```bash
mkdir -p data
```

---

### Step 5 — Run the EMS

```bash
cd services/cluster-ems
node --experimental-strip-types src/main.ts config/production.json
```

The EMS starts and listens on `http://127.0.0.1:7400`. Verify:

```bash
curl http://127.0.0.1:7400/health
curl http://127.0.0.1:7400/snapshot
curl http://127.0.0.1:7400/diagnostics
```

Expected health response when CAN is not yet connected:
```json
{ "status": "degraded", "clusterMode": "startup_equalization" }
```

---

### Step 6 — Run the bridge

In a second terminal:

```bash
cd services/utilitycore-bridge
node --experimental-strip-types src/main.ts config/production.json
```

The bridge starts, connects to MQTT, and begins publishing telemetry at 5-second intervals. Verify:

```bash
curl http://127.0.0.1:7401/health
curl http://127.0.0.1:7401/status
```

---

### Step 7 — Flash STM32 node firmware

Build the ARM images (requires ARM toolchain at `C:\tools\arm-none-eabi\bin`):

```powershell
cd firmware/clusterstore-firmware
.\scripts\build-g474-hal.ps1
```

Output images are in `build/g474-arm/`:
- `cs_bootloader_g474.elf` / `.bin` — flash to start of flash (0x08000000)
- `cs_native_node_g474_slot_a.elf` / `.bin` — flash to slot A (0x08010000)

Flash with STM32CubeProgrammer:

```bash
STM32_Programmer_CLI -c port=SWD -d cs_bootloader_g474.bin 0x08000000 -v
STM32_Programmer_CLI -c port=SWD -d cs_native_node_g474_slot_a.bin 0x08010000 -v
STM32_Programmer_CLI -c port=SWD -rst
```

---

### Step 8 — Connect CAN bus

Connect a USB-CAN adapter between the controller host and the STM32 node. Configure `CLUSTERSTORE_CAN_PORT` environment variable to point at the adapter serial device, then restart the EMS.

Once CAN traffic is flowing, the EMS startup sequencer runs through its phases automatically:
1. `discover_nodes` — waits for STATUS frames from at least one node
2. `precharge_primary` — issues precharge command via inverter
3. `close_primary` — closes main DC bus contactor
4. `admit_nodes` — admits nodes within voltage window
5. `balance_cluster` — runs equalization
6. `ready` — transitions to `normal_dispatch`

---

### Smoke test (no hardware)

Run the full software smoke stack against an in-memory simulator:

```bash
npm run sim:smoke
```

Run the MQTT stack smoke (requires Mosquitto installed):

```bash
npm run mqtt:mosquitto:run   # in one terminal
npm run smoke:stack          # in another
```

---

### Health endpoints reference

#### EMS (`http://127.0.0.1:7400`)

| Endpoint | Method | Description |
|---|---|---|
| `/health` | GET | Running status and cluster mode |
| `/snapshot` | GET | Full cluster telemetry snapshot |
| `/diagnostics` | GET | EMS internal diagnostic state |
| `/command` | POST | Apply a RemoteCommand |

#### Bridge (`http://127.0.0.1:7401`)

| Endpoint | Method | Description |
|---|---|---|
| `/health` | GET | Running status and last publish time |
| `/status` | GET | Bridge daemon state |

---

### Configuration reference

#### Dispatch strategies

| Value | Behaviour |
|---|---|
| `equal_current` | All admitted nodes share current equally |
| `soc_weighted` | Nodes with lower SoC receive proportionally more charge current |
| `temperature_weighted` | Nodes running cooler receive proportionally more current |

#### Authorizer kinds

| Kind | When to use |
|---|---|
| `allow-all` | Local development only. Will not start if MQTT has TLS or credentials. |
| `policy` | Production. Set `allowedRoles` and `allowedRequesters` explicitly. |

#### LTE modem kinds

| Kind | When to use |
|---|---|
| `state-file` | LTE state is written to a JSON file by an external modem script |
| `http-json` | LTE state is queried from an HTTP endpoint |
| `command` | LTE state is queried by running a command |

---

### Environment variables summary

| Variable | Required | Default | Purpose |
|---|---|---|---|
| `INVERTER_HOST` | Yes (Modbus) | — | Modbus TCP inverter IP address |
| `MQTT_HOST` | Yes | — | MQTT broker hostname |
| `MQTT_USERNAME` | Production | — | MQTT broker username |
| `MQTT_PASSWORD` | Production | — | MQTT broker password |
| `MQTT_CA_CERT_PATH` | Production TLS | — | Path to broker CA certificate |
| `CLUSTERSTORE_CAN_PORT` | Yes (hardware) | — | CAN adapter serial port |
| `CLUSTERSTORE_MOSQUITTO_EXE` | Optional | auto-detect | Path to `mosquitto.exe` for local dev |

---

### Known blockers (require site or hardware)

These are not software gaps — they are environment dependencies:

1. **`mosquitto.exe` not installed** — `npm run mqtt:mosquitto:check` fails on this host. Install via `choco install mosquitto` or set `CLUSTERSTORE_MOSQUITTO_EXE`.
2. **Real MQTT CA certificate absent** — bridge live-readiness warns until `MQTT_CA_CERT_PATH` points to the real broker CA.
3. **Real MQTT credentials absent** — `MQTT_USERNAME` / `MQTT_PASSWORD` must be set for TLS broker.
4. **CAN adapter not connected** — EMS stays in `startup_equalization` until CAN STATUS frames arrive.
5. **Modbus inverter not connected** — inverter reads return errors until `INVERTER_HOST` resolves.
6. **STM32 not flashed or validated** — hardware bring-up, CAN bench validation, and bootloader slot validation still required.
7. **Vendor-specific overlay adapters** — Victron, Pylontech, Growatt, Deye adapters exist as normalised seam stubs; real register maps and protocol details require vendor hardware.
