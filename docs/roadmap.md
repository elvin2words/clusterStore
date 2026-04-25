# Initial Delivery Roadmap

## Phase 1: Contract Freeze

- Finalize CAN arbitration IDs, payload packing, timeout windows, and multi-frame diagnostics strategy.
- Freeze MQTT topics, telemetry schema, alerts, command acknowledgements, and remote command safety constraints.
- Define per-site configuration bundles for electrical limits, inverter type, tariff mode, and installed node count.

## Phase 2: Embedded Integration

- Wire STM32 CAN driver to the shared message schema.
- Implement the cluster-aware node state machine and EMS supervision timeout.
- Add contactor feedback, precharge logic, and node isolation controls.

## Phase 3: EMS MVP

- Implement node registry, startup equalization, equal-current dispatch, and degraded-mode fallback.
- Add Modbus drivers for the first supported inverter family.
- Add local HMI screens for node health, cluster state, and active alarms.

## Phase 4: UtilityCore Bridge

- Implement MQTT publish and subscribe flows with acknowledgements.
- Add LTE watchdogs, store-and-forward buffering, and reconnection logic.
- Expose local SCADA registers for integrator-facing telemetry.

## Phase 5: Fleet Intelligence

- Add UtilityCore ClusterStore dashboards and alerting rules.
- Add cycle analytics, degradation tracking, and site performance reports.
- Introduce optimization features such as SoC-weighted dispatch and tariff-aware charge scheduling.

