# Git Packaging Plan

This plan groups the current uncommitted work into commit-ready sets without forcing everything into one giant commit.

## Commit 1

Message:

- `feat(services): ship runnable EMS and UtilityCore bridge stack`

Suggested paths:

- `package.json`
- `package-lock.json`
- `.npmrc`
- `services/cluster-ems/**`
- `services/utilitycore-bridge/**`
- `services/utilitycore-bridge/config/mosquitto/**`
- `scripts/clusterstore-can-adapter.mjs`
- `scripts/clusterstore-watchdog-adapter.mjs`
- `scripts/fake-mqtt-broker-cli.mjs`
- `scripts/live-readiness-check.mjs`
- `scripts/smoke-daemon-stack.ps1`
- `scripts/full-audit.ps1`
- `scripts/local-mosquitto.ps1`
- `tests/all.test.mjs`
- `tests/overlay-bms-adapter.test.mjs`
- `tests/support/fake-modbus-server.mjs`
- `tests/support/fake-mqtt-broker.mjs`

## Commit 2

Message:

- `feat(firmware): add portable STM32G474 workspace and HAL-backed builds`

Suggested paths:

- `firmware/clusterstore-firmware/**`
- `firmware/node-firmware/include/cluster_*`
- `firmware/node-firmware/src/cluster_*`
- `firmware/node-firmware/include/cluster_flash_layout.h`
- `firmware/node-firmware/src/cluster_flash_layout.c`
- `tests/firmware-binding.test.mjs`
- `tests/support/firmware-binding-runtime.mjs`

## Commit 3

Message:

- `docs(ops): add deployment guides, audits, roadmap, and repo runbooks`

Suggested paths:

- `.gitignore`
- `README.md`
- `docs/architecture.md`
- `docs/clusterstore-master-plan.md`
- `docs/current-audit.md`
- `docs/deployment-guide.md`
- `docs/git-packaging-plan.md`
- `docs/local-mosquitto-setup.md`
- `docs/master-implementation-walkthrough.md`
- `docs/next-development-roadmap.md`
- `docs/node-deployment-modes.md`
- `docs/operations-runbook.md`
- `docs/target-state-audit-2026-04-29.md`

## Review Before Commit

These files look like local working notes rather than production repo assets. Review them deliberately before including them in any commit:

- `clusterStoreDev.md`
- `currentLockIn.md`
- `mydevSteps.md`

If they are meant to stay private scratch notes, do not include them in the commit groups above.

---
---

git add package.json package-lock.json .npmrc services/cluster-ems services/utilitycore-bridge services/utilitycore-bridge/config/mosquitto scripts/clusterstore-can-adapter.mjs scripts/clusterstore-watchdog-adapter.mjs scripts/fake-mqtt-broker-cli.mjs scripts/live-readiness-check.mjs scripts/smoke-daemon-stack.ps1 scripts/full-audit.ps1 scripts/local-mosquitto.ps1 tests/all.test.mjs tests/overlay-bms-adapter.test.mjs tests/support/fake-modbus-server.mjs tests/support/fake-mqtt-broker.mjs

git commit -m "feat(services): ship runnable EMS and UtilityCore bridge stack"

