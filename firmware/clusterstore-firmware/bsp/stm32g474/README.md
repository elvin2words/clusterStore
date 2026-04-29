# STM32G474 BSP

Implemented contents:

- `cs_bsp.h`
- `cs_bsp_g474.h/.c`
- `cs_cluster_bridge_g474.h/.c`
- `cs_flash_g474.h/.c`
- `cs_can_g474.h/.c`
- `cs_ina228.h/.c`
- `cs_adc_g474.h/.c`
- `cs_iwdg_g474.h/.c`
- `cube_generated/`
- `startup/`
- `bench/`

## Bring-Up Defaults

- MCU: `STM32G474RET6`
- CAN: `FDCAN1`, `PB8` RX / `PB9` TX, `500 kbps` nominal, classic CAN mode for MVP
- FDCAN timing in the current BSP assumes an `80 MHz` FDCAN kernel clock
- Current sense: `INA228` on `I2C1`
- ADC: `ADC1` for pack voltage, bus voltage, and NTC temperature
- Contactor feedback defaults for the current board tree use `PB0` / `PB1` so debug UART can stay on `PA2` / `PA3`
- Watchdog: `IWDG`
- Flash page size: `2048 bytes`

The portable logic in `lib/` is intended to compile without any STM32 headers. Only this BSP layer should depend on Cube/HAL output.
