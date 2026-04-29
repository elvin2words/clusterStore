# Deployment Guide

This guide is the strict sequence to take the repo from local validation into a site deployment.

## 1. Prepare the Controller Host

1. Install the pinned toolchain versions from [package.json](/C:/Users/W2industries/Downloads/clusterStore/package.json).
2. Run `cmd /c npm install`.
3. Run `cmd /c npm run audit:full`.

Do not move to site commissioning until every step above passes on the exact deployment workstation or build runner.

## 2. Fill the Required Environment Variables

The live configs now support environment placeholders. Set these before bringing up the live daemons:

- `CLUSTERSTORE_SITE_ID`
- `CLUSTERSTORE_CLUSTER_ID`
- `CLUSTERSTORE_MODBUS_HOST`
- `CLUSTERSTORE_MODBUS_PORT`
- `CLUSTERSTORE_MODBUS_UNIT_ID`
- `CLUSTERSTORE_MQTT_HOST`
- `CLUSTERSTORE_MQTT_PORT`
- `CLUSTERSTORE_MQTT_CLIENT_ID`
- `CLUSTERSTORE_MQTT_USERNAME`
- `CLUSTERSTORE_MQTT_PASSWORD`
- `CLUSTERSTORE_MQTT_SERVERNAME`
- `CLUSTERSTORE_MQTT_CA_CERT_PATH`
- `CLUSTERSTORE_MODEM_STATE_URL`
- `CLUSTERSTORE_EMS_BASE_URL`
- `CLUSTERSTORE_ALLOWED_REQUESTER`

If you keep the example defaults, the system remains structurally valid but it is not production-ready.

## 3. Prepare the Runtime Inputs

1. Put the MQTT CA certificate at the path referenced by `CLUSTERSTORE_MQTT_CA_CERT_PATH`.
2. Populate the real CAN adapter input paths defined in [example.can-adapter.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.can-adapter.json), or replace that adapter config with your site-specific paths.
3. Confirm the watchdog consumer or supervisor owns the paths defined in [example.watchdog-adapter.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.watchdog-adapter.json), or replace that adapter with the real local supervisor command path.
4. Confirm the Modbus map in [example.live.daemon.json](/C:/Users/W2industries/Downloads/clusterStore/services/cluster-ems/config/example.live.daemon.json) matches the real inverter register map.
5. Confirm the MQTT requester allow-list in [example.secure.daemon.json](/C:/Users/W2industries/Downloads/clusterStore/services/utilitycore-bridge/config/example.secure.daemon.json) matches the real UtilityCore requester identity.

## 4. Validate the Site Config

1. Run `cmd /c npm run check:live-readiness`.
2. Run `node scripts/live-readiness-check.mjs --ems services/cluster-ems/config/example.live.daemon.json --bridge services/utilitycore-bridge/config/example.secure.daemon.json --probe`.

Step 1 checks structure, local adapter references, and environment-backed values.
Step 2 also probes the configured EMS, MQTT, LTE, and Modbus endpoints.

Do not continue if either command reports a `fail`.

## 5. Bring Up the Services

1. Start the EMS daemon with the live config:
   `node services/cluster-ems/dist/services/cluster-ems/src/daemon.js --config services/cluster-ems/config/example.live.daemon.json`
2. Confirm `GET /health`, `GET /snapshot`, and `GET /diagnostics` behave as expected.
3. Start the bridge daemon with the secure config:
   `node services/utilitycore-bridge/dist/services/utilitycore-bridge/src/daemon.js --config services/utilitycore-bridge/config/example.secure.daemon.json`
4. Confirm `GET /health` and `POST /publish-cycle` succeed.

## 6. Commission the External Systems

1. Capture live CAN status traffic and confirm the adapter output matches the `NodeStatusFrame` contract.
2. Read the real inverter registers and confirm every mapped field returns the expected scale and sign.
3. Verify the MQTT client can connect with TLS and publish telemetry.
4. Verify the bridge can receive and acknowledge a safe test command.
5. Force an LTE outage and confirm telemetry buffers and replays cleanly when connectivity returns.
6. Confirm the watchdog path is actively consumed by the real local supervisor.

## 7. Flash and Validate Firmware

1. Flash `cs_bootloader_g474`.
2. Flash one of `cs_native_node_g474_slot_a` or `cs_native_node_g474_slot_b`.
3. Confirm boot metadata, slot handoff, and rollback behavior on hardware.
4. Confirm watchdog refresh, CAN traffic, and fault latching on the STM32 target.

## 8. Production Exit Criteria

The system is ready only when all of the following are true:

- Local checks, builds, tests, smoke runs, and firmware builds are green.
- `check:live-readiness` reports no failures.
- The CA certificate and broker password are real, not placeholders.
- The CAN adapter emits real node status frames.
- The Modbus map is proven against the actual inverter.
- The bridge publishes telemetry and processes a safe command round-trip.
- LTE buffering and recovery have been exercised.
- The watchdog path is consumed by a real supervisor.
- The STM32 bootloader and runtime images have been flashed and validated on hardware.
