# Current Audit

Date: `2026-04-29` _(updated from 2026-04-26)_

## Scope

This audit covers the current committed state of the firmware, EMS, bridge, and tooling work across:

- `firmware/clusterstore-firmware`
- `firmware/node-firmware`
- `services/cluster-ems`
- `services/utilitycore-bridge`
- `packages/contracts`
- repo-level scripts, tests, CI, and documentation

---

## What Is Verified Right Now

| Check | Status |
|---|---|
| `npm run check` | PASS |
| `npm test` | PASS |
| `npm run build` | PASS |
| `npm run test:firmware-binding` | PASS |
| `npm run test:overlay-adapter` | PASS |
| `npm run sim:smoke` | PASS |
| `npm run smoke:stack` | PASS |
| `npm run check:live-readiness` | PASS (expected warnings on missing credentials) |
| `firmware:check` (check-firmware-env.ps1) | PASS — C:\tools toolchains present |
| `firmware:build:arm` (build-g474-hal.ps1) | PASS — produces 4 ARM images |
| `npm run mqtt:mosquitto:check` | FAIL — mosquitto.exe not installed on this host |

**ARM build output:**
- `cs_can_bench_g474`
- `cs_native_node_g474_slot_a`
- `cs_native_node_g474_slot_b`
- `cs_bootloader_g474`

Host build: `firmware/clusterstore-firmware` CTest suite — **4/4 passing** under `build/host-mingw`.

---

## Changes Since Previous Audit (2026-04-26)

The following work has been committed since the last audit:

### Newly Committed

1. **`firmware/node-firmware` new modules** — `cluster_bootloader_runtime`, `cluster_node_runtime`, `cluster_persistent_state`, `cluster_stm32_boot`, `cluster_stm32_hal` (headers + implementations)
2. **`firmware/clusterstore-firmware` BSP updates** — STM32Cube G4 Drivers tree imported under `cube_generated/Drivers/`, `cs_syscalls.c` bare-metal stubs, `build-g474-hal.ps1` build script, BSP CMakeLists updated
3. **ARM toolchain fix** — `cmake/arm-none-eabi.cmake` updated; clean-configure now default
4. **HAL config updates** — `stm32g4xx_hal_conf.h`, `cs_cube_g474_board.c`, `system_stm32g4xx.c` aligned to working ARM build
5. **Contracts** — `packages/contracts/package.json` and `tsconfig.json` updated
6. **Root tsconfig** — `tsconfig.base.json` updated
7. **Docs baseline** — full documentation set committed: architecture, master-plan, current-audit, deployment-guide, git-packaging-plan, local-mosquitto-setup, master-implementation-walkthrough, next-development-roadmap, node-deployment-modes, operations-runbook, target-state-audit-2026-04-29

---

## Key Findings

### Finding 1 — Portable Core and Native STM32 Runtime Are Still Split (unchanged)

The portable firmware modules under `firmware/clusterstore-firmware/lib/` are host-validated, but the STM32 app and bootloader targets still link through the older `firmware/node-firmware` runtime path.

The new modules added in `firmware/node-firmware` (`cluster_stm32_hal.c`, `cluster_stm32_boot.c`, `cluster_node_runtime.c`, `cluster_bootloader_runtime.c`, `cluster_persistent_state.c`) begin to bridge this gap, but full convergence — where the host-tested portable modules are the same code linked into target images — is still not complete.

**Status: OPEN — Priority 1 work**

### Finding 2 — ARM Build Is Now Working (resolved since 2026-04-26)

The STM32Cube G4 driver tree is now imported. The ARM build passes and produces all four target images. The previous hard stop on missing `Drivers/` is resolved.

**Status: RESOLVED**

### Finding 3 — FDCAN IRQ Path Wired (resolved since 2026-04-26)

The FDCAN IRQ callback surface is now wired into application entry points and `FDCAN1_IT0_IRQn` is enabled. Board still left in polling mode until hardware bring-up proves the interrupt path.

**Status: RESOLVED**

### Finding 4 — Overlay Adapter Identifier Resolution Fixed (resolved previously)

Commands now require all provided identifiers (`nodeId`, `nodeAddress`) to resolve to the same overlay asset. Previously any single matching identifier would permit the command.

**Status: RESOLVED**

### Finding 5 — No Real Mosquitto on This Host (unchanged)

`mosquitto.exe` is not installed on this workstation. `npm run mqtt:mosquitto:check` fails. Repo-side integration is complete.

**Status: OPEN — requires manual install or `CLUSTERSTORE_MOSQUITTO_EXE` env var**

---

## Ordered Way Forward

1. ~~Import the real STM32Cube G4 `Drivers/` tree~~ — **DONE**
2. ~~Build `cs_can_bench_g474`, `cs_bootloader_g474`, `cs_native_node_g474_slot_a` with ARM toolchain~~ — **DONE**
3. **Converge firmware runtimes** — migrate the STM32 image entry points off the older `firmware/node-firmware` path onto the portable `lib/` modules now proven in host tests
4. **Run first CAN bench** on a NUCLEO-G474RE plus USB-CAN adapter
5. **Add HIL coverage** for contactor sequencing, supervision timeout fallback, and boot rollback
6. **Extend overlay path** beyond the normalized adapter seam into vendor-specific adapters (Victron, Pylontech, Growatt, Deye)
7. **Install mosquitto.exe** on development host for real local broker path

## Practical Rule For The Repo (unchanged)

Until runtime convergence is complete, treat `firmware/clusterstore-firmware/lib/` as the validated portable reference implementation and treat the STM32 native image path as integration scaffolding still being aligned to it.

Do not treat "tests pass" as equivalent to "site ready." The system is only fully operational when audit and smoke checks are green, live-readiness warnings are cleared with real credentials, CAN and Modbus paths are proven on actual site hardware, and STM32 images are flashed and validated on hardware.
