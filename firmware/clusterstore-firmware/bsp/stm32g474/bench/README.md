# STM32G474 CAN-Only Bench

This is the first hardware-in-the-loop step for the native ClusterStore node path.

Goal:

- prove `FDCAN1` bring-up on a real `NUCLEO-G474RE`
- verify the node can transmit and receive classical CAN frames at `500 kbps`
- validate the internal-flash, INA228, ADC, and IWDG BSP seams without energizing contactors or HV hardware

## Hardware

- `NUCLEO-G474RE`
- external CAN transceiver board
  Example: `SN65HVD230`, `TJA1051`, or equivalent
- USB-CAN adapter
  Example: `CANable Pro`, `PEAK PCAN-USB`, `ValueCAN`
- laptop running Python with `python-can`
- 120 ohm termination across `CAN_H` / `CAN_L` if your adapter/transceiver pair does not already provide it

## Default Wiring

The BSP assumes these defaults for the first bring-up board:

- `PB8` -> `FDCAN1_RX`
- `PB9` -> `FDCAN1_TX`
- `I2C1` on `PB6` / `PB7` for `INA228`
- `ADC1` channels on `PA4` / `PA5` / `PA6`
- `PB0` / `PB1` for precharge and main contactor feedbacks
- `IWDG` as the watchdog peripheral

For the CAN bench:

- `PB9` -> transceiver `TXD`
- `PB8` -> transceiver `RXD`
- `3V3` -> transceiver `VCC`
- `GND` -> transceiver `GND`
- transceiver `CAN_H` / `CAN_L` -> USB-CAN adapter `CAN_H` / `CAN_L`

## Bench Runtime

The bench runtime lives in [cs_can_bench_node.c](../../../app/cs_can_bench_node.c).

It uses these IDs:

- heartbeat: `0x650 + node_id`
- command: `0x750 + node_id`
- ack: `0x760 + node_id`

Commands:

- `0x01` ping
- `0x02` sample-now

Heartbeat payload:

- bytes `0..1`: pack voltage in decivolts, little-endian
- bytes `2..3`: bus voltage in decivolts, little-endian
- bytes `4..5`: current in deciamps, signed little-endian
- byte `6`: temperature in whole degrees C
- byte `7`: flags
  - bit 0: current valid
  - bit 1: voltages valid
  - bit 2: temperature valid

## Python Harness

Use [can_bench.py](can_bench.py) to talk to the bench runtime.

Examples:

```bash
python can_bench.py monitor --interface socketcan --channel can0 --node-id 1
python can_bench.py ping --interface socketcan --channel can0 --node-id 1
python can_bench.py sample --interface socketcan --channel can0 --node-id 1
```

On Windows with a compatible adapter, the interface name may be `pcan`, `vector`, or `slcan` depending on the hardware and driver stack.

## Recommended Bring-Up Order

1. Build and flash `cs_can_bench_g474`, which calls `cs_bsp_g474_init()`, `cs_bsp_g474_start_can()`, and `cs_can_bench_node_step()` in the main loop.
   The current BSP timing helper expects an `80 MHz` FDCAN kernel clock from Cube/HAL.
2. Confirm the CAN transceiver is powered and termination is correct.
3. Run `monitor` and confirm heartbeat frames arrive once per second.
4. Run `ping` and confirm an ACK frame returns.
5. Run `sample` and confirm an immediate heartbeat plus ACK.
6. Only after this is stable, wire the native node app and then the overlay `bms_adapter` surface above it.
