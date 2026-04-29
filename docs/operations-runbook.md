# Operations Runbook

This runbook captures the verified commands and the remaining external prerequisites needed to move from local validation to a live site deployment.

## Local Verification

Run these from the repository root on this Windows workspace:

1. `cmd /c npm install`
2. `cmd /c npm run audit:full`

`audit:full` runs the repo's repeatable local validation sequence:

- `check`
- `test`
- `build`
- `test:firmware-binding`
- `test:overlay-adapter`
- `sim:smoke`
- `smoke:stack`
- `check:live-readiness`
- `firmware:check`
- `firmware:build:arm`

For a real local MQTT daemon instead of the fake smoke broker, use:

- `cmd /c npm run mqtt:mosquitto:check`
- `cmd /c npm run mqtt:mosquitto:run`

`smoke:stack` starts:

- a fake local MQTT broker
- the EMS daemon CLI
- the UtilityCore bridge daemon CLI
- file-backed EMS state inputs
- file-backed LTE state inputs

It verifies:

- EMS health endpoint startup
- bridge health endpoint startup
- EMS snapshot generation
- bridge MQTT publication through the real daemon entrypoint

## Firmware Workflow

Use the portable firmware workspace under `firmware/clusterstore-firmware`.

Recommended command path:

1. `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/check-firmware-env.ps1`
2. `powershell -ExecutionPolicy Bypass -File firmware/clusterstore-firmware/scripts/build-g474-hal.ps1`

The ARM build produces:

- `cs_can_bench_g474`
- `cs_native_node_g474_slot_a`
- `cs_native_node_g474_slot_b`
- `cs_bootloader_g474`

Notes:

- The build is configured for `MinGW Makefiles` on this machine because that path was reliable here.
- `build-g474-hal.ps1` now performs a clean configure by default so stale CMake toolchain cache does not silently poison later builds. Use `-ReuseBuildDirectory` only when you intentionally want an incremental firmware build.
- The cross-build now links against local bare-metal syscall stubs, so the default ARM build completes without the previous `nosys` fallback warnings.

## Service Runtime Configs

Local examples:

- `services/cluster-ems/config/example.daemon.json`
- `services/cluster-ems/config/example.live.daemon.json`
- `services/cluster-ems/config/example.can-adapter.json`
- `services/cluster-ems/config/example.watchdog-adapter.json`
- `services/utilitycore-bridge/config/example.daemon.json`

Secure bridge template:

- `services/utilitycore-bridge/config/example.secure.daemon.json`

The secure bridge template demonstrates:

- environment-backed site, broker, and secret placeholders
- MQTT over TLS
- explicit CA certificate path
- policy-based command authorization
- HTTP-polled LTE modem state

Validate a candidate live config set with:

1. `cmd /c npm run check:live-readiness`
2. `node scripts/live-readiness-check.mjs --ems <ems-config> --bridge <bridge-config> --probe`

The first command performs structural validation only.
The second command also probes the configured EMS, MQTT, LTE, and Modbus endpoints when those real systems are reachable.
Until you replace the secure example placeholders, the validator will intentionally warn about the missing CA file and placeholder MQTT password.

## Live Integration Checklist

These are the remaining real-world dependencies that cannot be proven from the repo alone.

### CAN

- Confirm physical bus bitrate, termination, transceiver wiring, and node addressing.
- Capture live status traffic and verify it normalizes into the `NodeStatusFrame` contract expected by EMS.
- Validate supervision timeout behavior when one node stops transmitting.

### Modbus

- Supply the vendor-specific register map for the actual inverter model.
- Confirm register scaling, word order, writable register semantics, and any mode bits for charge/discharge/export control.
- Run precharge and hold-open operations against the real inverter before enabling dispatch.

### MQTT

- Provision the production broker hostname, port, credentials, CA chain, and any ACLs.
- Verify retained/clean-session policy and command topic ACLs for the bridge client id.
- Confirm telemetry, command, and acknowledgement topics with the UtilityCore cloud side.

### LTE

- Provide the real modem state endpoint or command adapter used on site.
- Validate RSSI reporting, online/offline transitions, and outage recovery behavior.
- Test bridge buffering and replay with an actual uplink drop.

### Cloud / UtilityCore

- Provide service credentials, requester identities, and the allowed authorization roles.
- Confirm command TTL, idempotency, and acknowledgement semantics end to end.
- Dry-run maintenance mode, dispatch mode, and clear-fault command flows before enabling force charge/discharge in production.

## Recommended Deployment Sequence

1. Run `npm run audit:full`.
2. Build the STM32 images with `build-g474-hal.ps1`.
3. Bring up EMS with real CAN and inverter paths but local bridge auth policy.
4. Bring up the bridge against the real broker with telemetry only.
5. Enable command intake once MQTT, LTE, and requester policy are confirmed.
6. Run a commissioning drill that exercises startup, dispatch, offline buffering, recovery, and safe shutdown.

For the strict site-side sequence, use `docs/deployment-guide.md`.
