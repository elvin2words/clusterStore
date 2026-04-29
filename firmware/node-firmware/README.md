# Node Firmware Cluster Integration

This folder captures the cluster-specific embedded interfaces that sit alongside the existing STM32 BMS and MPPT firmware.

## Responsibilities

- Publish compact `NODE_STATUS` frames on CAN.
- Receive `NODE_CMD` frames from the Cluster EMS.
- Expose diagnostics through segmented `NODE_DIAG` messages.
- Transition between standalone and cluster-aware operating states.
- Fall back to standalone-safe behavior if EMS supervision is lost.

## Modules

- `cluster_can_protocol.*`: explicit byte-level CAN payload builders and parsers for status, command, and diagnostic frames.
- `cluster_command_manager.*`: command freshness, supervision timeout enforcement, sequence tracking, and local rejection of unsafe EMS commands.
- `cluster_contactor_manager.*`: contactor and precharge state machine with timeout supervision, feedback validation, and welded-contactor latching.
- `cluster_current_ramp.*`: charge and discharge ramp limiting so current setpoint changes are applied safely instead of as step changes.
- `cluster_state_machine.*`: cluster-aware node operating states, including balancing, isolation, standalone-safe fallback, and service lockout.
- `cluster_event_journal.*`: ring-buffer event journal designed to be flushed into non-volatile storage by a board-specific persistence callback.
- `cluster_ota_manager.*`: candidate image registration, trial boot activation, confirmation, and rollback bookkeeping for an OTA bootloader flow.
- `cluster_node_controller.*`: orchestration layer that ties commands, contactors, lockouts, journaling, OTA, and status publishing into one runtime surface.
- `cluster_persistent_state.*`: non-volatile metadata image that persists boot control and journal metadata using dual metadata copies.
- `cluster_node_runtime.*`: board-facing runtime that samples measurements, drains CAN commands, drives contactors, publishes status, and synchronizes OTA state into persistent storage.
- `cluster_bootloader_runtime.*`: dual-slot bootloader flow that restores persistent state, validates trial images, decrements attempt counters, and chooses the correct slot to boot.
- `cluster_stm32_hal.*` / `cluster_stm32_boot.*`: STM32-oriented adapter layer for HAL-backed CAN/GPIO/watchdog hooks and a jump helper for slot handoff.

## Integration Notes

- Keep the node address in the CAN arbitration ID so the payload can stay compact.
- Use a command supervision timeout on every received cluster command.
- Track the last accepted command sequence and a local heartbeat counter for diagnostics, service tooling, and stale-command detection.
- Treat the local BMS as the final authority. Cluster commands are advisory within node safety limits.
- Only close contactors when local interlocks and precharge checks pass.
- Reject cluster commands locally when they violate current limits, lockout states, balancing policy, or timeout policy.
- Serialize CAN payloads explicitly; do not transmit C structs directly as raw wire payloads.
- Persist the event journal metadata and records in board-specific NVM so reboot recovery can restore the rolling audit log.
- Use the OTA manager state together with a dual-slot bootloader so new firmware runs in a trial state until the application confirms itself healthy.
- Keep boot control and journal metadata in a double-buffered metadata area so the node can recover cleanly after a reset during a write.
- Treat the STM32 HAL port as the board-support seam: pin maps, ADC scaling, internal flash erase/program details, and watchdog choice should live there rather than inside the controller modules.
