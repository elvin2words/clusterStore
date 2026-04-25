# ClusterStore — Full Stack Conception

---

## Overview

The Cluster EMS is the **coordination brain** of the system — the software layer running on cluster controller hardware that bridges node-level firmware up to UtilityCore. It handles everything from CAN bus mastery to cloud telemetry, and is capable of running on a DIN Rail Industrial PC.

### Core Cluster EMS Capabilities

| Capability | Description |
|---|---|
| **CAN Bus Master** | Polls all nodes; aggregates SoC, voltage, and temperature data |
| **Modbus TCP/RTU Master** | Communicates with the grid-side inverter/charger |
| **LTE/4G Modem Interface** | Uploads telemetry to UtilityCore |
| **Local HMI Interface** | Small touchscreen or 4-button + LCD for field diagnostics |
| **Watchdog & Supervisory Logic** | If the controller crashes, nodes default to safe standalone mode |

---

## Software Components

> *Differentiating ClusterStore from "just a battery rack."*

The software stack has **three distinct layers**.

---

### 1 Node-Level Firmware
*Runs on each PowerHive MCU*

Each node runs its own embedded firmware — STM32, BMS control loop, MPPT algorithm. Key additions for ClusterStore:

#### CAN Bus Driver & Message Schema

The message schema is the **API between node hardware and the Cluster EMS**.

| Frame | Payload |
|---|---|
| `NODE_STATUS` | SoC (%), voltage (mV), current (mA), temperature (°C), fault flags |
| `NODE_CMD` | Charge setpoint (A), discharge setpoint (A), mode |
| `NODE_DIAG` | Cell-level voltages, cycle count, cumulative kWh throughput |

> **Broadcast cadence:** 100 ms for real-time data · 1 000 ms for diagnostics

#### Cluster-Aware State Machine

Additional FSM states beyond standalone operation:

- `CLUSTER_SLAVE_CHARGE`
- `CLUSTER_SLAVE_DISCHARGE`
- `CLUSTER_BALANCING` — node accepts charge to equalise SoC with peers
- `CLUSTER_ISOLATED` — safely disconnected from bus pending maintenance

---

### 2 Cluster EMS
*Runs on the cluster controller*

The most technically interesting software in the system. Responsible for:

#### SoC Balancing Across Nodes

> **The fundamental problem:** Two nodes on the same bus with different SoCs will equalise through the bus, causing uncontrolled current flow.

The EMS prevents this by:
1. Reading all node SoCs at startup **before** connecting them
2. Sequencing contactor closures — lowest-SoC node connects first, receives controlled charge from the highest-SoC node via the cluster controller's DC-DC
3. Enforcing a **SoC equalisation phase** before entering normal cluster operation

#### Charge Dispatch Algorithm

| Strategy | Logic | Note |
|---|---|---|
| **Equal-Current Dispatch** | Each node gets 1/N of available charge current | ✅ Sufficient for initial commercial product |
| **SoC-Weighted Dispatch** | Lower-SoC nodes receive more current | Faster equalisation |
| **Temperature-Weighted Dispatch** | Hot nodes are derated | Protects longevity |

> More sophisticated algorithms become a **software update path** post-launch.

#### Load Dispatch & Grid Interaction

- **Node priority order:** Highest SoC first, or round-robin for balanced aging
- **Demand response:** Grid cheap → prefer grid; grid expensive/unavailable (load shedding) → cluster discharge
- **Export management:** Excess energy exported at a defined rate if grid-tied

#### Fault Handling

On any node fault (over-temperature, cell overvoltage, BMS trip), the EMS:

1. Opens that node's contactor
2. Redistributes allocated current to remaining nodes
3. Logs the event with timestamp and node ID
4. Sends an alert via the MQTT bridge
5. Continues operating in **degraded mode**

---

### 3 Communications Stack

#### Intra-Cluster
- **CAN bus** at 500 kbps between cluster controller and all node MCUs

#### Inverter/Charger Interface
- **Modbus RTU / Modbus TCP** to the grid-side hybrid inverter
- Compatible with: Victron MultiPlus, Growatt, Deye
- Reads: AC input voltage/frequency, AC output load, DC bus voltage
- Writes: Charge/discharge setpoints

#### Cloud Uplink — MQTT over LTE/4G → UtilityCore

| Topic | Behaviour |
|---|---|
| `cluster/{site_id}/telemetry` | Real-time data · published every 60 seconds |
| `cluster/{site_id}/alerts` | Fault and alarm events · published immediately |
| `cluster/{site_id}/cmd` | Command subscription for remote dispatch |

#### Local Comms
- **Modbus TCP over Ethernet/WiFi** — for existing SCADA integration
- **RS-485 Modbus RTU** — fallback for clients without Ethernet infrastructure

---

### 4 Data Model

Key telemetry fields per cluster, per **1-minute interval**:

| Category | Fields |
|---|---|
| **Cluster State** | SoC (%), aggregate capacity (kWh), available energy (kWh) |
| **Power Flow** | Charge/discharge power (W), cumulative energy in/out (kWh) |
| **Node Breakdown** | Per-node SoC array |
| **AC Output** | Voltage (V), frequency (Hz), load (W) |
| **Solar** | Site generation (W) — if connected |
| **Faults** | Active fault code array |

> ⚠️ This schema drives the **UtilityCore dashboard design** — it must be defined at the architecture stage, before development begins.

---

## Operational Intelligence
*UtilityCore · MAAS · MAAP*

---

### UtilityCore as ClusterStore's Operations Layer

UtilityCore ingests MQTT telemetry from deployed clusters via its IoT data pipeline. It requires a **ClusterStore-specific asset type** with a dedicated dashboard template:

- Per-node SoC bars
- Cluster aggregate energy curve
- Cycle count tracking
- Fault event log

---

### MAAS on Top of ClusterStore

> *This is the commercial unlock.* Every ClusterStore deployment becomes a MAAS-subscribed asset.

| Tier | Features | Monthly Rate |
|---|---|---|
| **Tier 1 — Basic** | Real-time SoC visibility, basic fault alerts | $ |
| **Tier 2 — Analytics** | Energy yield reports, degradation tracking, cycle analytics | $ |
| **Tier 3 — Managed** | Remote dispatch optimisation, predictive fault detection, SLA-backed response | $ |

---

### MAAP — The API Layer at Scale

As ClusterStore deployments accumulate, **MAAP** becomes the API that lets UtilityCore treat all of them as a single managed fleet.

> This is the **VPP (Virtual Power Plant) thesis at scale** — coordinating distributed storage assets across multiple sites.

```
PowerHive Nodes  ──CAN──►  Cluster EMS  ──MQTT/LTE──►  UtilityCore  ──API──►  MAAP Fleet Layer
```