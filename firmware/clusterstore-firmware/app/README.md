# Node Application Images

This directory now contains both the first CAN-only bench image and the native node image wrapper that binds the STM32G474 BSP into the existing node-controller runtime.

Implemented contents:

- [main.c](main.c): CAN-only bench image entry point
- [native_main.c](native_main.c): native node image entry point
- [cs_can_bench_node.c](cs_can_bench_node.c): CAN-only bring-up loop
- [cs_native_node_runtime.c](cs_native_node_runtime.c): controller/contactors/feedbacks runtime wrapper
- [cs_g474_board_defaults.c](cs_g474_board_defaults.c): shared board-default BSP configuration
- `linker/`: Slot A and Slot B linker scripts

The native node image uses the exact agreed flash map:

- Slot A at `0x08010000`
- Slot B at `0x08040000`

Current limitation:

- `cs_native_node_g474_*` still links through `firmware/node-firmware` for the boot/runtime/controller flow. The newer `lib/` portable modules are validated on host, but they are not yet the runtime used by the native STM32 image targets.
