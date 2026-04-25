# Capability Map

This table translates the ClusterStore pillars into a concrete MVP scope and highlights the missing capabilities we should treat as first-class requirements.

| Domain | Capability | Owner Layer | Priority | Notes |
| --- | --- | --- | --- | --- |
| Node control | BMS enforcement and power protection | Node firmware | Must have | Remains locally authoritative even under EMS control |
| Node control | MPPT / local converter loops | Node firmware | Must have | Existing embedded control pillar |
| Node control | CAN status and command handling | Node firmware | Must have | Shared contract seeded in `packages/contracts` |
| Node control | Cluster-aware FSM states | Node firmware | Must have | Adds balancing, isolated, and slave states |
| Node control | Supervision timeout to standalone-safe mode | Node firmware | Must have | Required for watchdog strategy |
| Cluster control | Node discovery and health registry | Cluster EMS | Must have | Tracks online, degraded, isolated nodes |
| Cluster control | Startup SoC equalization sequencing | Cluster EMS | Must have | Prevents uncontrolled equalization currents |
| Cluster control | Equal-current dispatch | Cluster EMS | Must have | Commercial MVP algorithm |
| Cluster control | SoC-weighted dispatch | Cluster EMS | Should have | Software-upgrade path |
| Cluster control | Temperature-weighted dispatch | Cluster EMS | Should have | Software-upgrade path |
| Cluster control | Fault isolation and degraded operation | Cluster EMS | Must have | Keep cluster online when one node fails |
| Cluster control | Inverter integration over Modbus | Cluster EMS | Must have | RTU and TCP variants needed |
| Cluster control | Local HMI / field diagnostics | Cluster EMS | Must have | Touchscreen or button-plus-LCD path |
| Cluster control | Hardware watchdog / supervisor integration | Cluster EMS | Must have | Controller crash must fail safe |
| Cluster control | Maintenance and lockout mode | Cluster EMS | Must add | Needed for field service workflows |
| Cluster control | Commissioning workflow | Cluster EMS | Must add | Site bring-up, calibration, naming, acceptance |
| Cluster control | Precharge / contactor interlock logic | Cluster EMS | Must add | Electrical safety and bus health |
| Cluster control | Local event journal | Cluster EMS | Must add | Needed for field diagnostics when cloud is offline |
| Comms | MQTT telemetry uplink | UtilityCore bridge | Must have | Publish every 60 seconds |
| Comms | Immediate alerts topic | UtilityCore bridge | Must have | Faults and alarms publish instantly |
| Comms | Remote command subscription | UtilityCore bridge | Must have | Guardrailed, validated, acked |
| Comms | Command acknowledgement topic | UtilityCore bridge | Must add | Needed for deterministic remote ops |
| Comms | Local SCADA integration | UtilityCore bridge | Must have | Modbus TCP/RTU and Ethernet integration |
| Comms | Telemetry store-and-forward | UtilityCore bridge | Must add | Required for unreliable LTE conditions |
| Comms | Secure device identity / MQTT auth | UtilityCore bridge | Must add | Certificate or token lifecycle |
| Comms | LTE modem observability | UtilityCore bridge | Must add | Signal quality, SIM state, reconnection metrics |
| Fleet ops | ClusterStore asset type in UtilityCore | UtilityCore platform | Must have | Dashboard and analytics template |
| Fleet ops | Per-node SoC bars and fault log | UtilityCore platform | Must have | Core operational UI |
| Fleet ops | Cycle analytics and degradation tracking | UtilityCore platform | Should have | Tier 2 MAAS value |
| Fleet ops | Remote dispatch optimization | UtilityCore platform | Should have | Tier 3 MAAS value |
| Fleet ops | Predictive fault detection | UtilityCore platform | Could have | Advanced analytics path |
| Platform | Versioned telemetry schemas | Shared contracts | Must add | Protects UtilityCore compatibility |
| Platform | Time sync and monotonic event ordering | Shared contracts | Must add | Important for diagnostics and billing |
| Platform | OTA update workflow | Shared platform | Must add | Controller plus nodes |
| Platform | Simulation / replay / HIL harness | Shared platform | Must add | Needed before field scale-out |

