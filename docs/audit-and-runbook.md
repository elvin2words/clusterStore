# ClusterStore — Audit and Runbook

Date: `2026-04-30`

---

## Part 1 — Current State Audit

### What is fully implemented

| Layer | Component | State |
|---|---|---|
| Contracts | CAN wire protocol, MQTT envelope, all TypeScript types | Complete |
| EMS | Startup sequencer (6 phases), fault manager, dispatch (3 strategies), force charge/discharge with TTL | Complete |
| EMS | Remote command validation (temporal, role, scope, TTL, NaN guard) | Complete |
| EMS | Modbus TCP client (FC03 read, FC06 write, FC16 write-multiple, exception decoding) | Complete |
| EMS | CAN adapter (state-file, overlay-file, command subprocess modes) | Complete |
| EMS | Journal, HMI, watchdog, BMS overlay adapters | Complete |
| EMS | HTTP control API (6 endpoints) | Complete |
| Bridge | MQTT client (custom 3.1.1, TLS, keepalive) | Complete |
| Bridge | Command ledger, idempotency, dedup on reconnect | Complete |
| Bridge | Telemetry buffer, LTE offline replay | Complete |
| Bridge | Authorization (allow-all and policy modes) | Complete |
| Bridge | SCADA fanout, journal | Complete |
| Firmware | Portable core: boot control, journal, platform vtable, CAN protocol | Complete |
| Firmware | STM32G474 BSP: FDCAN, flash, ADC, INA228, IWDG | Complete (no-HAL host-tested) |
| Firmware | Node: OTA, state machine, contactor, ramp, persistent state | Complete |
| Scripts | CAN adapter CLI, watchdog CLI, fake MQTT broker, smoke stack | Complete |

---

### Remaining gaps

| Gap | Severity | File | Notes |
|---|---|---|---|
| Firmware runs on host only — no HAL cross-compile validation | **Critical (for hardware)** | `firmware/clusterstore-firmware/` | NUCLEO-G474RE bring-up still needed; CAN is polling by default |
| Interrupt-driven CAN not validated on real silicon | High | `firmware/node-firmware/src/cluster_stm32_hal.c` | Handler wired but polling is active default |
| Node isolation not replayed on EMS restart | Medium | `services/cluster-ems/src/runtime.ts` | Isolates written to JSONL but not re-read on daemon start |
| Command ledger grows unbounded | Medium | `services/utilitycore-bridge/src/runtime.ts` | No TTL or size limit; long-running daemon slowly leaks memory |
| Inverter temperature register not mapped | Medium | `services/cluster-ems/src/runtime.ts` | `ModbusTcpGridInverterConfig.stateMap` has no temp field; overtemp is a monitoring blind spot |
| MQTT reconnect has no jitter or backoff | Low | Bridge `runtime.ts` | Multiple bridges could thunderherd reconnect on broker restart |
| Modbus failures have no backoff | Low | `services/cluster-ems/src/runtime.ts sendRequest` | Sequential per-cycle retries flood logs on inverter fault |
| `Date.parse()` monotonicity assumption | Low | `startup-sequencer.ts`, `ems-controller.ts` | No guard against host clock skew mid-operation |
| File atomic writes use POSIX rename semantics | Low | `writeJsonFileAtomic` throughout | Safe on Linux; a reader holding the file on Windows can break the `.tmp` swap |
| Power mismatch threshold hardcoded | Low | `services/cluster-ems/src/ems-controller.ts:576` | 500 W + 10 % not tunable per site |

---

### HTTP endpoints

**EMS daemon (default port 8081)**

| Method | Path | Returns |
|---|---|---|
| `GET` | `/health` | Daemon state: running, startedAt, lastCycleAt, lastSuccessAt, lastError |
| `GET` | `/snapshot` | Full `ClusterTelemetry` object |
| `GET` | `/alerts` | Pending alerts; add `?drain=true` to clear the queue |
| `GET` | `/diagnostics` | Node diagnostic frames |
| `POST` | `/run-cycle` | Runs a control cycle immediately and returns telemetry |
| `POST` | `/commands` | Applies a `RemoteCommand` and returns `CommandAcknowledgement` |

**Bridge daemon (default port 8082)**

| Method | Path | Returns |
|---|---|---|
| `GET` | `/health` | Daemon state: running, startedAt, lastPublishAt, lastSuccessAt, lastError |
| `POST` | `/publish-cycle` | Triggers one telemetry drain and publish cycle |

---

### MQTT topics

| Direction | Topic pattern | Purpose |
|---|---|---|
| Subscribe | `cluster/{siteId}/{clusterId}/cmd` | Incoming remote commands |
| Publish | `cluster/{siteId}/{clusterId}/telemetry` | EMS snapshot (publish interval, default 60 s) |
| Publish | `cluster/{siteId}/{clusterId}/alerts` | Fault and alert events |
| Publish | `cluster/{siteId}/{clusterId}/cmd_ack` | Command acknowledgements |

All messages use `MqttEnvelope` (schema version `"1.0.0"`, `sentAt` ISO timestamp, `payload` field).

---

### External connections summary

| System | Protocol | Direction | Required for |
|---|---|---|---|
| STM32 nodes | CAN 500 kbps | Bidirectional | Any hardware deployment |
| Grid inverter | Modbus TCP port 502 | EMS reads/writes | Real dispatch and precharge |
| MQTT broker (Mosquitto) | MQTT 3.1.1 TCP or TLS | Bridge bidirectional | Cloud commands and telemetry |
| LTE modem | File / HTTP / subprocess | Bridge reads | Offline buffering decisions |
| SCADA system | File write | Bridge writes | Local monitoring |
| Watchdog / supervisor | File / subprocess | EMS writes | Process health enforcement |
| Fleet management / cloud API | MQTT inbound via broker | Inbound | Remote control |
| Journal consumer | JSONL file read | External reads | Ops and audit trail |

---

## Part 2 — Running the System

### Prerequisites

```
Node.js   >= 22 LTS
npm       >= 10
cmake     >= 3.22        (firmware only)
arm-none-eabi-gcc >= 12  (firmware cross-compilation only)
```

Check installed versions:

```bash
node --version && npm --version
cmake --version
arm-none-eabi-gcc --version   # firmware only
```

Install dependencies from the repo root:

```bash
npm install
```

Build TypeScript (generates `dist/` in each service):

```bash
npm run build
```

Type-check without emitting:

```bash
npm run check
```

Run the test suite (17 integration tests, EMS + Bridge):

```bash
npm test
```

---

### Local simulation — no hardware, no external broker

This mode runs the full control stack against JSON files. You don't need Mosquitto, a real inverter, or CAN hardware.

Run the test suite (EMS + Bridge integration, 17 tests):

**Smoke stack (starts EMS + Bridge + fake MQTT broker as real OS processes):**

```powershell
# Windows PowerShell
.\scripts\smoke-daemon-stack.ps1

# Leave processes running after the script finishes:
.\scripts\smoke-daemon-stack.ps1 -LeaveRunning
```

The script wires everything up with temp files and prints a JSON summary showing `clusterMode` and `mqttMessages` count. If both have non-zero / valid values the full stack is functioning.

**End-to-end smoke scenario without real daemons:**

```bash
node scripts/smoke-simulator.mjs
```

---

### Manual daemon startup — file-based adapters

This runs a real EMS process you can query over HTTP, with no real hardware required.

**Step 1 — Create the runtime directory and write initial input files:**

```bash
mkdir -p runtime/ems

cat > runtime/ems/statuses.json << 'EOF'
[
  {
    "nodeAddress": 1, "nodeId": "node-01", "ratedCapacityKwh": 5,
    "socPct": 50, "packVoltageMv": 51200, "packCurrentMa": 0,
    "temperatureDeciC": 250, "faultFlags": [],
    "contactorClosed": false, "readyForConnection": true,
    "balancingActive": false, "maintenanceLockout": false,
    "serviceLockout": false, "heartbeatAgeMs": 0
  },
  {
    "nodeAddress": 2, "nodeId": "node-02", "ratedCapacityKwh": 5,
    "socPct": 52, "packVoltageMv": 51300, "packCurrentMa": 0,
    "temperatureDeciC": 248, "faultFlags": [],
    "contactorClosed": false, "readyForConnection": true,
    "balancingActive": false, "maintenanceLockout": false,
    "serviceLockout": false, "heartbeatAgeMs": 0
  }
]
EOF

cat > runtime/ems/inverter-state.json << 'EOF'
{
  "acInputVoltageV": 230, "acInputFrequencyHz": 50,
  "acOutputVoltageV": 230, "acOutputFrequencyHz": 50,
  "acOutputLoadW": 0, "dcBusVoltageV": 51.2,
  "gridAvailable": true, "solarGenerationW": 0,
  "availableChargeCurrentA": 10, "requestedDischargeCurrentA": 0,
  "exportAllowed": false, "tariffBand": "normal"
}
EOF
```

**Step 2 — Create the EMS config (`runtime/ems/daemon.json`):**

```json
{
  "config": {
    "siteId": "site-alpha",
    "clusterId": "cluster-01",
    "aggregateCapacityKwh": 10,
    "maxChargeCurrentPerNodeA": 20,
    "maxDischargeCurrentPerNodeA": 20,
    "equalizationWindowPct": 5,
    "controlLoopIntervalMs": 2000,
    "telemetryIntervalMs": 60000,
    "supervisionTimeoutMs": 1500,
    "defaultDispatchStrategy": "equal_current",
    "startup": {
      "voltageMatchWindowMv": 500,
      "prechargeTimeoutMs": 10000,
      "contactorSettleTimeoutMs": 3000,
      "balancingTimeoutMs": 30000,
      "balancingMaxCurrentA": 5,
      "startupTimeoutMs": 120000,
      "minNodesForDispatch": 2
    },
    "remoteCommands": {
      "maxCommandTtlMs": 900000,
      "maxChargeOverrideCurrentA": 20,
      "maxDischargeOverrideCurrentA": 20,
      "allowedRolesByType": {
        "force_charge": ["fleet_controller", "service"],
        "force_discharge": ["fleet_controller", "service"],
        "set_dispatch_mode": ["fleet_controller", "service", "operator"],
        "set_maintenance_mode": ["service", "technician"],
        "clear_fault_latch": ["service", "technician"]
      }
    }
  },
  "cycle": { "intervalMs": 2000, "runOnStart": true },
  "http": { "host": "127.0.0.1", "port": 8081 },
  "canBus": {
    "kind": "state-file",
    "statusesPath": "./runtime/ems/statuses.json",
    "diagnosticsPath": "./runtime/ems/diagnostics.json",
    "commandsPath": "./runtime/ems/commands.json",
    "commandHistoryPath": "./runtime/ems/command-history.jsonl",
    "isolatesPath": "./runtime/ems/isolates.jsonl"
  },
  "inverter": {
    "kind": "state-file",
    "statePath": "./runtime/ems/inverter-state.json",
    "setpointPath": "./runtime/ems/inverter-setpoint.json"
  },
  "hmi": { "kind": "console" },
  "watchdog": {
    "kind": "file",
    "heartbeatPath": "./runtime/ems/watchdog.json",
    "failSafePath": "./runtime/ems/failsafe.jsonl"
  },
  "journal": { "kind": "jsonl-file", "path": "./runtime/ems/journal.jsonl" }
}
```

**Step 3 — Start the EMS:**

```bash
node services/cluster-ems/dist/services/cluster-ems/src/daemon.js \
  --config runtime/ems/daemon.json
```

**Verify:**

```bash
curl http://127.0.0.1:8081/health
curl http://127.0.0.1:8081/snapshot
```

---

## Part 3 — Live Hardware Deployment

### CAN bus integration

The EMS communicates with battery nodes through a CAN adapter. Three modes are supported.

#### Mode A: state-file

An external CAN driver writes node status JSON to a file; the EMS reads it each cycle and writes commands atomically to another file.

```json
{
  "canBus": {
    "kind": "state-file",
    "statusesPath": "/var/clusterstore/can/statuses.json",
    "commandsPath": "/var/clusterstore/can/commands.json",
    "isolatesPath": "/var/clusterstore/can/isolates.jsonl"
  }
}
```

Your CAN driver owns `commandsPath` for reading and `statusesPath` for writing. The EMS polls `statusesPath` at the configured `controlLoopIntervalMs`.

#### Mode B: command subprocess (recommended for the bundled CAN adapter script)

The EMS forks `scripts/clusterstore-can-adapter.mjs` on each cycle.

```json
{
  "canBus": {
    "kind": "command",
    "readStatuses": {
      "command": "node",
      "args": ["scripts/clusterstore-can-adapter.mjs", "--config", "./can-adapter.json", "read-statuses"]
    },
    "writeCommands": {
      "command": "node",
      "args": ["scripts/clusterstore-can-adapter.mjs", "--config", "./can-adapter.json", "write-commands"]
    },
    "isolateNode": {
      "command": "node",
      "args": ["scripts/clusterstore-can-adapter.mjs", "--config", "./can-adapter.json", "isolate-node"]
    }
  }
}
```

The CAN adapter config (`can-adapter.json`) points to your hardware driver's file paths:

```json
{
  "statusesPath": "/var/clusterstore/can/statuses.json",
  "diagnosticsPath": "/var/clusterstore/can/diagnostics.json",
  "commandsPath": "/var/clusterstore/can/commands.json",
  "commandHistoryPath": "/var/clusterstore/can/command-history.jsonl",
  "isolatesPath": "/var/clusterstore/can/isolates.jsonl"
}
```

#### Mode C: overlay-file (non-native BMS assets — Victron, Pylontech, BYD, etc.)

For sites where an external BMS integration already normalises battery telemetry into a JSON file.

```json
{
  "canBus": {
    "kind": "overlay-file",
    "assetsPath": "/var/clusterstore/bms/assets.json",
    "dispatchPath": "/var/clusterstore/bms/dispatch.json"
  }
}
```

Expected schema per asset entry in `assets.json`:

```json
{
  "assetId": "batt-01",
  "nodeId": "node-01",
  "nodeAddress": 1,
  "ratedCapacityKwh": 10,
  "socPct": 55,
  "packVoltageMv": 51200,
  "packCurrentMa": 0,
  "temperatureDeciC": 250,
  "faultFlags": [],
  "contactorClosed": true,
  "readyForConnection": true,
  "balancingActive": false,
  "maintenanceLockout": false,
  "serviceLockout": false,
  "heartbeatAgeMs": 100
}
```

---

### Modbus TCP inverter

Configure under `inverter` in the EMS daemon config.

```json
{
  "inverter": {
    "kind": "modbus-tcp",
    "host": "${CLUSTERSTORE_MODBUS_HOST}",
    "port": 502,
    "unitId": 1,
    "timeoutMs": 5000,
    "stateMap": {
      "acInputVoltageV":           { "address": 3,   "type": "u16", "scale": 10 },
      "acInputFrequencyHz":        { "address": 9,   "type": "u16", "scale": 100 },
      "acOutputVoltageV":          { "address": 15,  "type": "u16", "scale": 10 },
      "acOutputFrequencyHz":       { "address": 21,  "type": "u16", "scale": 100 },
      "acOutputLoadW":             { "address": 23,  "type": "u16" },
      "dcBusVoltageV":             { "address": 26,  "type": "u16", "scale": 100 },
      "gridAvailable":             { "address": 50,  "type": "bool" },
      "solarGenerationW":          { "address": 108, "type": "u16" },
      "availableChargeCurrentA":   { "address": 60,  "type": "u16", "scale": 10 },
      "requestedDischargeCurrentA":{ "address": 62,  "type": "u16", "scale": 10 },
      "exportAllowed":             { "address": 70,  "type": "bool" },
      "tariffBand": {
        "address": 80, "type": "tariff-band",
        "values": { "0": "cheap", "1": "normal", "2": "expensive" },
        "defaultValue": "normal"
      },
      "meteredSitePowerW":         { "address": 100, "type": "i16" }
    },
    "setpointMap": {
      "operatingMode": {
        "address": 200, "type": "enum",
        "values": { "idle": 0, "charge": 1, "discharge": 2 }
      },
      "aggregateChargeCurrentA":    { "address": 201, "type": "u16", "scale": 10 },
      "aggregateDischargeCurrentA": { "address": 202, "type": "u16", "scale": 10 },
      "exportLimitW":               { "address": 203, "type": "u16" }
    }
  }
}
```

Register address mapping is **inverter-model-specific**. The numbers above are illustrative. Read your inverter's Modbus register map and replace every `address` value before deploying.

Supported register types:

| Type | Width | Notes |
|---|---|---|
| `u16` | 1 register | Unsigned 16-bit. Optional `scale` divides raw value. |
| `i16` | 1 register | Signed 16-bit (two's complement). Optional `scale`. |
| `u32` | 2 registers | Optional `wordOrder`: `"msw-first"` (default) or `"lsw-first"`. |
| `i32` | 2 registers | Same as `u32` with sign extension. |
| `bool` | 1 register | `trueValues` array (default `[1]`). For writes: `trueValue`/`falseValue`. |
| `tariff-band` | 1 register | Raw integer → `TariffBand` string via `values` map. |
| `enum` | 1 register | String → integer via `values` map. Write-only. |

---

### MQTT broker

The bridge connects to any MQTT 3.1.1 broker. Mosquitto is the reference choice.  The bridge connects to it and manages all cloud telemetry and command routing.

**Install Mosquitto (Ubuntu/Debian):**

```bash
sudo apt-get install mosquitto mosquitto-clients
```

**Minimal `mosquitto.conf` for local/dev:**

```
listener 1883
allow_anonymous true
```

**Production `mosquitto.conf` with TLS and authentication:**

```
listener 8883
cafile   /etc/mosquitto/ca.crt
certfile /etc/mosquitto/server.crt
keyfile  /etc/mosquitto/server.key
require_certificate false
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Create a broker user:

```bash
mosquitto_passwd -c /etc/mosquitto/passwd clusterstore-bridge
```

**Bridge MQTT config block:**

```json
{
  "mqtt": {
    "kind": "mqtt-tcp",
    "host": "${CLUSTERSTORE_MQTT_HOST}",
    "port": 8883,
    "clientId": "clusterstore-bridge-site-alpha",
    "username": "${CLUSTERSTORE_MQTT_USERNAME}",
    "password": "${CLUSTERSTORE_MQTT_PASSWORD}",
    "keepAliveSeconds": 30,
    "tls": {
      "enabled": true,
      "serverName": "${CLUSTERSTORE_MQTT_SERVERNAME}",
      "caCertPath": "${CLUSTERSTORE_MQTT_CA_CERT_PATH}"
    }
  }
}
```

---

### LTE modem

Three modes for reading modem connectivity state:

**state-file** — an external process writes a JSON file:

```json
{ "online": true, "signalRssiDbm": -78, "carrier": "Vodacom" }
```

```json
{ "lte": { "kind": "state-file", "path": "/var/clusterstore/lte/modem.json" } }
```

**http-json** — bridge polls an HTTP endpoint returning the same schema:

```json
{ "lte": { "kind": "http-json", "url": "${CLUSTERSTORE_MODEM_STATE_URL}", "timeoutMs": 3000 } }
```

**command** — bridge forks a subprocess per check:

```json
{
  "lte": {
    "kind": "command",
    "isOnline": { "command": "mmcli", "args": ["-m", "0", "--output-json"] }
  }
}
```

When `isOnline` returns `false`, the bridge enqueues telemetry to the local buffer file instead of publishing to MQTT. On reconnect, the buffer is drained in order up to `replayBatchSize` messages per publish cycle.

---

### SCADA integration

The bridge writes telemetry and alerts to local files. The SCADA system reads them on its own schedule.

```json
{
  "scada": {
    "kind": "file",
    "telemetryPath": "/var/clusterstore/scada/telemetry.json",
    "alertsPath":    "/var/clusterstore/scada/alerts.jsonl"
  }
}
```

- `telemetry.json` is atomically overwritten each publish cycle with the full `ClusterTelemetry` object.
- `alerts.jsonl` is append-only JSONL, one `ClusterAlert` per line.

---

### Authorization

**Development — allow-all (no TLS/credentials allowed alongside this):**

```json
{ "authorizer": { "kind": "allow-all" } }
```

**Production — policy (role and requester whitelist):**

```json
{
  "authorizer": {
    "kind": "policy",
    "allowedRoles":      ["fleet_controller", "service"],
    "allowedRequesters": ["fleet@yourcompany.com", "scada@yourcompany.com"]
  }
}
```

`allowedRoles` is matched against `command.authorization.role`. `allowedRequesters` is matched against `command.requestedBy`. Both must pass.

---

### Watchdog adapter

The EMS kicks a heartbeat file on every successful control cycle. A supervisor (systemd watchdog, hardware watchdog daemon, or custom script) reads that file.

**file mode:**

```json
{
  "watchdog": {
    "kind": "file",
    "heartbeatPath": "/var/clusterstore/ems/watchdog.json",
    "failSafePath":  "/var/clusterstore/ems/failsafe.jsonl"
  }
}
```

`watchdog.json` is atomically rewritten with `{ "timestamp": "...", "ok": true }` every cycle. If the EMS crashes, the file goes stale. An external watchdog that monitors file mtime can detect this.

`failsafe.jsonl` is appended when `triggerFailSafe` is called (i.e. on unhandled exceptions in `runCycle`).

**command mode (call a real hardware watchdog or supervisory script):**

```json
{
  "watchdog": {
    "kind": "command",
    "kick": {
      "command": "node",
      "args": ["scripts/clusterstore-watchdog-adapter.mjs", "--config", "./watchdog.json", "kick"]
    },
    "triggerFailSafe": {
      "command": "node",
      "args": ["scripts/clusterstore-watchdog-adapter.mjs", "--config", "./watchdog.json", "trigger-failsafe"]
    }
  }
}
```

---

## Part 4 — Full Production Config Reference

### Environment variables

Set these before starting either daemon. Values can be injected directly or via an `EnvironmentFile` in systemd.

```
CLUSTERSTORE_SITE_ID
CLUSTERSTORE_CLUSTER_ID
CLUSTERSTORE_MODBUS_HOST
CLUSTERSTORE_MODBUS_PORT
CLUSTERSTORE_MODBUS_UNIT_ID
CLUSTERSTORE_MQTT_HOST
CLUSTERSTORE_MQTT_PORT
CLUSTERSTORE_MQTT_CLIENT_ID
CLUSTERSTORE_MQTT_USERNAME
CLUSTERSTORE_MQTT_PASSWORD
CLUSTERSTORE_MQTT_SERVERNAME
CLUSTERSTORE_MQTT_CA_CERT_PATH
CLUSTERSTORE_MODEM_STATE_URL
CLUSTERSTORE_EMS_BASE_URL
CLUSTERSTORE_ALLOWED_REQUESTER
```

### Full Bridge daemon config (`runtime/bridge/daemon.json`)

```json
{
  "bridge": {
    "siteId":          "${CLUSTERSTORE_SITE_ID}",
    "clusterId":       "${CLUSTERSTORE_CLUSTER_ID}",
    "maxCommandTtlMs": 900000,
    "replayBatchSize": 50
  },
  "publish": { "intervalMs": 60000, "runOnStart": true },
  "http":    { "host": "127.0.0.1", "port": 8082 },
  "mqtt": {
    "kind":     "mqtt-tcp",
    "host":     "${CLUSTERSTORE_MQTT_HOST}",
    "port":     8883,
    "clientId": "clusterstore-bridge-${CLUSTERSTORE_SITE_ID}",
    "username": "${CLUSTERSTORE_MQTT_USERNAME}",
    "password": "${CLUSTERSTORE_MQTT_PASSWORD}",
    "tls": {
      "enabled":    true,
      "serverName": "${CLUSTERSTORE_MQTT_SERVERNAME}",
      "caCertPath": "${CLUSTERSTORE_MQTT_CA_CERT_PATH}"
    }
  },
  "lte": {
    "kind": "state-file",
    "path": "/var/clusterstore/lte/modem.json"
  },
  "emsApi": {
    "baseUrl":   "${CLUSTERSTORE_EMS_BASE_URL}",
    "timeoutMs": 10000
  },
  "buffer": {
    "kind": "file",
    "path": "/var/clusterstore/bridge/buffer.json"
  },
  "scada": {
    "kind":         "file",
    "telemetryPath": "/var/clusterstore/scada/telemetry.json",
    "alertsPath":    "/var/clusterstore/scada/alerts.jsonl"
  },
  "authorizer": {
    "kind":              "policy",
    "allowedRoles":      ["fleet_controller", "service"],
    "allowedRequesters": ["${CLUSTERSTORE_ALLOWED_REQUESTER}"]
  },
  "commandLedger": {
    "kind": "file",
    "path": "/var/clusterstore/bridge/ledger.json"
  },
  "journal": {
    "kind": "jsonl-file",
    "path": "/var/clusterstore/bridge/journal.jsonl"
  }
}
```

**Start Bridge:**

```bash
export CLUSTERSTORE_SITE_ID=site-alpha
export CLUSTERSTORE_CLUSTER_ID=cluster-01
export CLUSTERSTORE_MQTT_HOST=your-broker.example.com
export CLUSTERSTORE_MQTT_USERNAME=clusterstore-bridge
export CLUSTERSTORE_MQTT_PASSWORD=secret
export CLUSTERSTORE_MQTT_SERVERNAME=your-broker.example.com
export CLUSTERSTORE_MQTT_CA_CERT_PATH=/etc/clusterstore/ca.crt
export CLUSTERSTORE_EMS_BASE_URL=http://127.0.0.1:8081
export CLUSTERSTORE_ALLOWED_REQUESTER=fleet@yourcompany.com

node services/utilitycore-bridge/dist/services/utilitycore-bridge/src/daemon.js \
  --config runtime/bridge/daemon.json
```

---

## Part 5 — Observability and Health Checks

**Curl the health endpoints:**

```bash
curl http://127.0.0.1:8081/health   # EMS
curl http://127.0.0.1:8082/health   # Bridge
```

Healthy EMS response: `running: true`, `lastSuccessAt` within the last `controlLoopIntervalMs`, no `lastError`.

**Structured health check via the live readiness script:**

```bash
node scripts/live-readiness-check.mjs \
  --ems     services/cluster-ems/config/example.live.daemon.json \
  --bridge  services/utilitycore-bridge/config/example.secure.daemon.json \
  --probe
```

This probes EMS HTTP, MQTT broker connectivity, LTE endpoint, and Modbus reachability in sequence.

**Watch the EMS journal in real time:**

```bash
tail -f /var/clusterstore/ems/journal.jsonl | jq .
```

Key `kind` values to monitor:

| kind | Meaning |
|---|---|
| `ems.fail_safe` | EMS threw an unhandled exception — check `message` immediately |
| `command.rejected` | A remote command was rejected — check `metadata.issues` |
| `command.applied` | A remote command was applied successfully |
| `alert.opened` | A fault incident opened |
| `alert.cleared` | A fault incident resolved |
| `bridge.buffering` | LTE is offline, telemetry is being buffered |
| `bridge.replay_failed` | MQTT replay failed — buffered messages accumulating |

**Force a control cycle manually:**

```bash
curl -X POST http://127.0.0.1:8081/run-cycle | jq .clusterMode
```

**Check active faults:**

```bash
curl http://127.0.0.1:8081/snapshot | jq '.activeFaults'
```

---

## Part 6 — Process Supervision

No bundled systemd units or Docker images are provided. The recommended approach is a systemd unit per daemon.

**`/etc/systemd/system/clusterstore-ems.service`:**

```ini
[Unit]
Description=ClusterStore EMS
After=network.target

[Service]
ExecStart=/usr/bin/node /opt/clusterstore/services/cluster-ems/dist/services/cluster-ems/src/daemon.js --config /etc/clusterstore/ems.json
EnvironmentFile=/etc/clusterstore/env
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**`/etc/systemd/system/clusterstore-bridge.service`:**

```ini
[Unit]
Description=ClusterStore UtilityCore Bridge
After=network.target clusterstore-ems.service

[Service]
ExecStart=/usr/bin/node /opt/clusterstore/services/utilitycore-bridge/dist/services/utilitycore-bridge/src/daemon.js --config /etc/clusterstore/bridge.json
EnvironmentFile=/etc/clusterstore/env
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

**`/etc/clusterstore/env`** — one variable per line, no `export`:

```
CLUSTERSTORE_SITE_ID=site-alpha
CLUSTERSTORE_CLUSTER_ID=cluster-01
CLUSTERSTORE_MQTT_HOST=broker.yoursite.example.com
CLUSTERSTORE_MQTT_USERNAME=clusterstore-bridge
CLUSTERSTORE_MQTT_PASSWORD=changeme
CLUSTERSTORE_MQTT_SERVERNAME=broker.yoursite.example.com
CLUSTERSTORE_MQTT_CA_CERT_PATH=/etc/clusterstore/ca.crt
CLUSTERSTORE_MODBUS_HOST=192.168.1.50
CLUSTERSTORE_EMS_BASE_URL=http://127.0.0.1:8081
CLUSTERSTORE_ALLOWED_REQUESTER=fleet@yourcompany.com
```

**Enable and start:**

```bash
systemctl daemon-reload
systemctl enable clusterstore-ems clusterstore-bridge
systemctl start  clusterstore-ems clusterstore-bridge
journalctl -fu clusterstore-ems
journalctl -fu clusterstore-bridge
```

---

## Part 7 — Sending Remote Commands

Commands arrive at the broker on `cluster/{siteId}/{clusterId}/cmd`. The payload must be `MqttEnvelope<RemoteCommand>`.

**Minimum valid `force_charge` payload:**

```json
{
  "schemaVersion": "1.0.0",
  "sentAt": "2026-04-30T10:00:00.000Z",
  "payload": {
    "id": "cmd-001",
    "idempotencyKey": "cmd-001",
    "sequence": 1,
    "type": "force_charge",
    "createdAt": "2026-04-30T10:00:00.000Z",
    "expiresAt":  "2026-04-30T10:15:00.000Z",
    "requestedBy": "fleet@yourcompany.com",
    "target": {
      "siteId":    "site-alpha",
      "clusterId": "cluster-01"
    },
    "authorization": {
      "tokenId":  "tok-001",
      "role":     "fleet_controller",
      "scopes":   ["cluster:force_charge"],
      "issuedAt": "2026-04-30T09:55:00.000Z",
      "expiresAt": "2026-04-30T10:55:00.000Z"
    },
    "payload": { "currentA": 10 }
  }
}
```

**Publish via mosquitto_pub (dev/test):**

```bash
mosquitto_pub \
  -h your-broker.example.com -p 1883 \
  -t "cluster/site-alpha/cluster-01/cmd" \
  -m "$(cat command.json)"
```

**Command validation rules** enforced at the bridge and again at the EMS:

| Check | Rule |
|---|---|
| `expiresAt` | Must be in the future; delta from now must not exceed `maxCommandTtlMs` |
| `authorization.expiresAt` | Must be in the future |
| `authorization.scopes` | Must contain `cluster:{command.type}` |
| `authorization.role` | Must be in `allowedRolesByType[command.type]` in EMS config |
| `sequence` | Must be strictly greater than the last accepted sequence number |
| `currentA` (force_charge / force_discharge) | Must be a finite positive number ≤ configured max |
| `startupCompleted` | Must be `true` for all commands except `set_maintenance_mode` |
| `target.siteId` / `target.clusterId` | Must match the controller's configured site and cluster |

**Acknowledgement lifecycle:**

| Status | Meaning |
|---|---|
| `accepted` | Bridge validated the command and forwarded to EMS |
| `completed` | EMS applied the command successfully |
| `rejected` | Validation failed at bridge or EMS level; `reason` field explains why |
| `duplicate` | A command with this `idempotencyKey` was already processed |

Acks arrive on `cluster/{siteId}/{clusterId}/cmd_ack`.

**All supported command types:**

| `type` | Required `payload` fields | Effect |
|---|---|---|
| `force_charge` | `currentA: number` | Override dispatch to charge at specified current until `expiresAt` |
| `force_discharge` | `currentA: number` | Override dispatch to discharge at specified current until `expiresAt` |
| `set_dispatch_mode` | `dispatchStrategy: "equal_current"\|"soc_weighted"\|"temperature_weighted"` | Change normal dispatch strategy |
| `set_maintenance_mode` | `enabled: boolean` | Enter/exit maintenance mode; isolates all nodes if `true` |
| `clear_fault_latch` | _(none)_ | Clears the fault latch if no active incidents remain |

---

## Part 8 — Firmware

### Host-side CTest (no hardware required)

```bash
cmake -B firmware/build-test -DCS_BUILD_TESTS=ON
cmake --build firmware/build-test
cd firmware/build-test && ctest --output-on-failure
```

Runs 6 tests: `test_boot_control`, `test_journal`, `test_cluster_platform`, `test_g474_bsp`, `test_node_boot_control`, `test_node_persistent_state`.

### Cross-compilation for STM32G474 (requires arm-none-eabi-gcc)

```bash
cmake -B firmware/build \
  -DCMAKE_TOOLCHAIN_FILE=firmware/clusterstore-firmware/cmake/arm-none-eabi.cmake \
  -DCS_G474_USE_HAL=ON \
  -DCS_BUILD_NATIVE_NODE_APP=ON
cmake --build firmware/build
```

Outputs:
- `firmware/build/app/cs_native_node_g474_slot_a.elf`
- `firmware/build/app/cs_native_node_g474_slot_b.elf`
- `firmware/build/boot/cs_bootloader_g474.elf`

### Flash to NUCLEO-G474RE

Flash the bootloader first (only needed once or when updating the bootloader):

```bash
arm-none-eabi-objcopy -O binary \
  firmware/build/boot/cs_bootloader_g474.elf \
  firmware/build/boot/bootloader.bin

st-flash write firmware/build/boot/bootloader.bin 0x08000000
```

Flash slot A:

```bash
arm-none-eabi-objcopy -O binary \
  firmware/build/app/cs_native_node_g474_slot_a.elf \
  firmware/build/app/slot_a.bin

st-flash write firmware/build/app/slot_a.bin 0x08010000
```

### Memory layout

| Region | Address | Size | Contents |
|---|---|---|---|
| Bootloader | `0x08000000` | 32 KB | `cs_bootloader_g474` |
| BCB-A | `0x08008000` | 4 KB | Boot Control Block for slot A |
| BCB-B | `0x08009000` | 4 KB | Boot Control Block for slot B |
| Event journal | `0x0800A000` | 24 KB | Fixed-record flash log |
| Slot A | `0x08010000` | 192 KB | Active firmware |
| Slot B | `0x08040000` | 192 KB | OTA candidate / fallback |
| Reserved | `0x08070000` | 64 KB | — |

On power-on the bootloader reads the BCB at `0x08008000`, validates the CRC32 on the selected slot, and jumps to it. If CRC fails it tries the other slot. If both fail it halts.

### CAN wire protocol (reference)

Operates at 500 kbps classic CAN.

| Frame | ID | Cadence | Direction |
|---|---|---|---|
| `NODE_STATUS` | `0x100 + nodeAddress` | 100 ms | Node → Controller |
| `NODE_CMD` | `0x200 + nodeAddress` | 100 ms | Controller → Node |
| `NODE_DIAG` | `0x300 + nodeAddress` | 1000 ms (segmented) | Node → Controller |

Frame encoding and decoding is handled by `packages/contracts/src/can.ts` on the TypeScript side and `firmware/node-firmware/src/cluster_can_protocol.c` on the firmware side.

---

## Part 9 — Architecture Diagram

```
     ┌─────────────────────────────────────────────────────────┐
     │  Battery nodes (STM32G474, one per battery string)      │
     │  Firmware: boot control, OTA, CAN protocol, state FSM   │
     └────────────────────────┬────────────────────────────────┘
                              │ CAN bus 500 kbps
                              │ NODE_STATUS 100ms / NODE_CMD 100ms
                              ▼
     ┌─────────────────────────────────────────────────────────┐
     │  Cluster EMS  (HTTP :8081)                              │
     │                                                         │
     │  ┌──────────────────┐  ┌──────────────────────────┐    │
     │  │ StartupSequencer │  │ FaultManager             │    │
     │  │ 6-phase state    │  │ latched incidents, cap   │    │
     │  │ machine          │  │ 256 max active           │    │
     │  └──────────────────┘  └──────────────────────────┘    │
     │  ┌──────────────────┐  ┌──────────────────────────┐    │
     │  │ DispatchAllocator│  │ RemoteCommandValidator   │    │
     │  │ equal / soc /    │  │ TTL, role, scope, NaN    │    │
     │  │ temperature      │  │ guard, sequence check    │    │
     │  └──────────────────┘  └──────────────────────────┘    │
     │                                                         │
     │  Adapters:                                              │
     │  ├─ CAN bus    (state-file | overlay-file | command)   │
     │  ├─ Modbus TCP (FC03 read / FC06 write / FC16 write)   │
     │  ├─ Watchdog   (file | command)                        │
     │  ├─ Journal    (jsonl-file | command)                  │
     │  └─ HMI        (console | file)                        │
     └────────────────────────┬────────────────────────────────┘
                              │ HTTP /snapshot /alerts /commands
                              ▼
     ┌─────────────────────────────────────────────────────────┐
     │  UtilityCore Bridge  (HTTP :8082)                       │
     │                                                         │
     │  ┌──────────────────┐  ┌──────────────────────────┐    │
     │  │ CommandRouter    │  │ TelemetryBuffer          │    │
     │  │ validate, dedup, │  │ LTE offline buffering    │    │
     │  │ idempotency key  │  │ ack-based replay         │    │
     │  └──────────────────┘  └──────────────────────────┘    │
     │  ┌──────────────────┐  ┌──────────────────────────┐    │
     │  │ CommandLedger    │  │ Authorizer               │    │
     │  │ seen / received  │  │ allow-all | policy       │    │
     │  │ / acks           │  │ (role + requester)       │    │
     │  └──────────────────┘  └──────────────────────────┘    │
     │                                                         │
     │  Integrations:                                          │
     │  ├─ MQTT broker  (publish / subscribe, optional TLS)   │
     │  ├─ LTE modem    (state-file | http-json | command)    │
     │  ├─ SCADA        (file: telemetry.json + alerts.jsonl) │
     │  └─ Journal      (jsonl-file | command)                │
     └────────────────────────┬────────────────────────────────┘
                              │ MQTT 3.1.1
                              ▼
     ┌─────────────────────────────────────────────────────────┐
     │  Cloud / Fleet management system                        │
     │  Publishes RemoteCommand to cmd topic                   │
     │  Receives ClusterTelemetry from telemetry topic         │
     │  Receives CommandAcknowledgement from cmd_ack topic     │
     └─────────────────────────────────────────────────────────┘
```
