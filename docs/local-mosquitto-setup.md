# Local Mosquitto Setup

This repo now supports a real local Mosquitto broker in addition to the fake broker used in automated smoke tests.

## What Is Already Wired

- [example.daemon.json](/C:/Users/W2industries/Downloads/clusterStore/services/utilitycore-bridge/config/example.daemon.json) already points the bridge at `127.0.0.1:1883`
- [local-dev.conf](/C:/Users/W2industries/Downloads/clusterStore/services/utilitycore-bridge/config/mosquitto/local-dev.conf) provides a local development broker config
- [local-mosquitto.ps1](/C:/Users/W2industries/Downloads/clusterStore/scripts/local-mosquitto.ps1) can verify or launch the broker

## Commands

Check whether `mosquitto.exe` is available:

- `cmd /c npm run mqtt:mosquitto:check`

Run the broker in the foreground:

- `cmd /c npm run mqtt:mosquitto:run`

Then start the services in separate terminals:

1. `cmd /c npm run start:ems`
2. `cmd /c npm run start:bridge`

## Host Requirement

This workstation audit found:

- no `mosquitto.exe` currently on PATH
- no `winget.exe` currently on PATH
- Chocolatey is present, but `choco search mosquitto` did not surface a direct package here

That means the repo-side integration is ready, but the host still needs a real Mosquitto binary installed manually or via your internal software distribution.

## Supported Ways To Point The Repo At Mosquitto

The launcher will resolve `mosquitto.exe` in this order:

1. `CLUSTERSTORE_MOSQUITTO_EXE`
2. `mosquitto.exe` on PATH
3. `C:\Program Files\mosquitto\mosquitto.exe`
4. `C:\Program Files (x86)\mosquitto\mosquitto.exe`

If you install Mosquitto outside those paths, set:

- `CLUSTERSTORE_MOSQUITTO_EXE=C:\full\path\to\mosquitto.exe`

## When To Use Which Broker

Use the fake broker when:

- you want fully self-contained automated tests
- you want deterministic smoke runs
- you do not want an external dependency

Use real local Mosquitto when:

- you want to test the bridge against a real MQTT daemon
- you want to inspect retained state, subscriptions, or broker logs
- you are preparing for broker-side ACL, TLS, or deployment work
