## What ClusterStore Is As A Product Right Now

ClusterStore is a **clustered BESS management platform**. The core idea is this: instead of one large monolithic battery system, you have multiple battery nodes — whether your own custom hardware or existing third-party BESS units — all coordinated by a single intelligent energy management layer that presents them to the outside world as one unified storage asset.

That unified asset then connects upward to UtilityCore, your SCADA/monitoring platform, which is where the operator sits and sees everything.

---

## The Full Stack — Layer By Layer

### Layer 1 — The Battery Nodes (Bottom)

Each node is one battery unit. You have two ways to deploy a node:

**Mode A — Native node (your own hardware)**
A custom PCB built around the STM32G474RET6 microcontroller. It sits physically inside or alongside a battery pack and owns everything — contactor control, precharge sequencing, current and voltage measurement, temperature monitoring, CAN communication, and OTA firmware updates. This is the hardware you'd manufacture or prototype for a PowerHive-branded product.

**Mode B — Overlay node (wrapping existing BESS)**
You clip onto an existing Pylontech, Victron, Growatt, or Deye unit via its RS485 or VE.Direct port. A small adapter board — essentially just a microcontroller with a Modbus or VE.Direct interface on one side and CAN on the other — reads SoC, voltage, current, and temperature from the existing BMS and translates it into the ClusterStore CAN protocol. The existing unit's own BMS still protects the cells. ClusterStore just adds coordination and visibility on top.

Both modes speak the same CAN wire protocol to the cluster controller. The EMS above them never knows the difference.

---

### Layer 2 — The Cluster CAN Bus (Middle)

All nodes in a cluster are wired together on a single FDCAN bus at 500kbps. Classic CAN topology — twisted pair, 120Ω termination at each end, up to roughly 40 metres at that speed which is plenty for a containerised or room-scale installation.

The bus carries four frame types:

- **Heartbeat** (node → controller, every 1 second) — node ID, state, SoC, voltage, current, temperature, fault flags
- **Command** (controller → node) — START, STOP, FAULT_CLEAR, SET_CHARGE_LIMIT, SET_DISCHARGE_LIMIT
- **Ack** (node → controller) — accepted, completed, or rejected, with reason
- **OTA** (controller → node) — firmware image chunks for over-the-air updates

---

### Layer 3 — The Cluster EMS (The Brain)

This is the TypeScript service running on the cluster controller — a small Linux computer (Raspberry Pi CM4, or STM32H743 if you want a pure embedded approach) that sits in the cabinet and owns the cluster bus.

It does six things continuously:

**1. Supervision** — every node must heartbeat within the supervision window (configurable, default ~1.5s). Miss it and the node is marked offline, contactors opened, fault logged.

**2. Startup sequencing** — when the cluster comes up, nodes are admitted one at a time in a defined order. Each node goes through discovery → precharge → voltage check → admission. The EMS won't admit a node whose pack voltage is too far from bus voltage. This prevents inrush currents that would stress contactors or trip protection.

**3. Dispatch** — the EMS receives a charge or discharge setpoint (from UtilityCore or a local schedule) and distributes it across active nodes based on SoC balancing. Nodes with higher SoC get more discharge, nodes with lower SoC get more charge. Nodes in fault are excluded from dispatch.

**4. Fault management** — faults are latched as incidents with unique IDs. The same fault doesn't spam upstream every cycle. Incidents have a lifecycle — open, acknowledged, resolved — and the EMS tracks which node triggered which incident.

**5. Remote command handling** — commands arriving from UtilityCore over MQTT are validated for TTL, auth metadata, idempotency, and target binding before being applied. The EMS sends back an accepted/completed/rejected ack to UtilityCore.

**6. Telemetry** — every cycle the EMS aggregates cluster state — total SoC (weighted, not naive mean), total power (from site metering if available, inferred from nodes otherwise), per-node detail — and publishes it upstream. If the LTE/network link drops, telemetry is buffered and replayed in order when connectivity restores.

---

### Layer 4 — The UtilityCore Bridge (Upward Connection)

The bridge service runs alongside the EMS on the cluster controller. It is the gateway between the cluster (CAN + local logic) and the outside world (UtilityCore cloud platform, SCADA systems).

It speaks MQTT over LTE or Ethernet to UtilityCore. Topics are structured per cluster and per node so multiple clusters at multiple sites all report into one UtilityCore instance.

Upstream it publishes:
- Cluster telemetry (SoC, power, energy, node count, state)
- Per-node telemetry (individual SoC, voltage, current, temperature, state)
- Fault incidents (open/resolved events with severity and node ID)
- Command acks (so UtilityCore knows whether a remote command was applied)

Downstream it receives:
- Remote commands (force charge, force discharge, set limits, stop, start)
- Configuration updates
- OTA trigger commands (tells the EMS to push a firmware update to a node)

It also fans out to SCADA via a separate channel — so if a site already has a SCADA system (Wonderware, Ignition, anything that speaks MQTT or Modbus TCP), ClusterStore can feed it without replacing it.

---

### Layer 5 — UtilityCore (Top)

UtilityCore is the cloud/operator platform. From the operator's perspective, ClusterStore is just a device that appears in UtilityCore with a live dashboard showing cluster health, SoC trend, power flow, active faults, and command history. The operator can issue remote commands, acknowledge faults, trigger OTA updates, and view historical data — all without knowing anything about the CAN bus or the EMS internals.

---

## What The Full System Looks Like Physically

```
[ UtilityCore Cloud ]
        ↕ MQTT over LTE/Ethernet
[ Cluster Controller Cabinet ]
  ├── Raspberry Pi CM4 (or similar)
  │     ├── EMS service (TypeScript)
  │     └── UtilityCore bridge (TypeScript)
  ├── USB-CAN adapter (or native FDCAN on controller SoC)
  └── LTE modem / Ethernet port
        ↕ FDCAN 500kbps, twisted pair
  ┌─────────────────────────────────┐
  │         CLUSTER CAN BUS         │
  └─────────────────────────────────┘
     ↕              ↕              ↕
[ Node 1 ]    [ Node 2 ]    [ Node 3 ]
Native STM32  Overlay       Overlay
hardware      Pylontech     Victron
              US3000        SmartShunt
```

---

## Hardware Stack — Native Standalone Node Module

For a production native node — the module you'd manufacture and sell or deploy — the hardware stack is:

**MCU:** STM32G474RET6
- Runs the node firmware (bootloader + app)
- Owns all peripheral control
- ~$4 USD in volume

**CAN transceiver:** TJA1051T/3 or SN65HVD230
- Converts STM32 FDCAN differential to the CAN bus
- ~$1 USD

**Current sensing:** INA228AIDGSR (I2C, 16-bit, hardware oversampling)
- Sits on the pack current shunt
- Feeds SoC estimation and protection
- ~$3 USD

**Voltage sensing:** Resistor divider network (pack voltage, bus voltage)
- Two channels into STM32 ADC1
- Cents in BOM cost

**Temperature sensing:** Two NTC thermistors (10kΩ at 25°C, B25/85=3950)
- Pack temperature and ambient
- Into STM32 ADC1
- Cents in BOM cost

**Contactor drive:** Two gate driver circuits
- Precharge contactor: typically a small relay or solid-state relay
- Main contactor: driven by STM32 GPIO → MOSFET → relay coil
- Contactor feedback: opto-isolated sense back to STM32 GPIO

**Power supply:** Small isolated DC-DC from pack voltage to 3.3V for MCU
- Something like a Recom R-78E3.3-0.5 or custom flyback
- MCU and sensing rails must be isolated from HV pack

**Connectors:**
- CAN bus: 2-pin (CANH, CANL)
- Pack terminals: HV+ and HV- (to battery pack)
- Bus terminals: HV+ and HV- (to cluster bus)
- Debug: 4-pin SWD header (for programming and debugging)

**Enclosure:** DIN-rail mountable, IP54 for cabinet use

**Rough BOM cost for native node module:** ~$30–50 USD in small volumes for electronics, excluding enclosure and contactors which are application-specific.

---

## What You Have Right Now vs What's Missing

**What you have:**
- Complete software architecture — contracts, EMS, bridge all verified
- Complete firmware architecture — bootloader logic, node FSM, BMS adapters, boot control, journal all written
- Correct memory map and flash layout defined for STM32G474
- CAN wire protocol aligned between TypeScript and C layers
- Hardware validation plan written

**What's still needed before first deployment:**

On software:
- Host C build running (toolchain install + cmake)
- CubeMX HAL tree generated (one-time setup)
- UART driver wired into Pylontech adapter (stub currently)
- CAN bench validated on real NUCLEO board

On hardware for native node:
- PCB schematic and layout (Altium or KiCad)
- PCB fabricated and assembled (JLCPCB or similar for prototypes)
- Firmware flashed and bench-tested against relay simulation rig

On hardware for overlay node:
- Much simpler — just a small adapter PCB with STM32, CAN transceiver, and RS485 or UART
- Or even just a Raspberry Pi with USB-RS485 and USB-CAN for early deployments

**The fastest path to first commercial deployment** is the overlay mode — wrap an existing Pylontech or Victron rack, run the cluster controller software on a Pi, connect to UtilityCore. No custom hardware, no PCB fab, deployed in a day. That's your MVP proof of concept you can put in front of a customer while native hardware is being designed.

---
---

