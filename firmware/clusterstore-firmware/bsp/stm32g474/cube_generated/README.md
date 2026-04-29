# STM32G474 Cube-Style Board Tree

This folder is where the STM32G474 board initialization, interrupt glue, and HAL configuration live for the first native-node bring-up.

What is included here now:

- `Inc/main.h`: board pin map and shared prototypes
- `Inc/cs_cube_g474_board.h`: board init entry points and HAL handle accessors
- `Inc/stm32g4xx_hal_conf.h`: HAL module enable list for this board surface
- `Inc/stm32g4xx_it.h`: interrupt handler declarations
- `Src/cs_cube_g474_board.c`: `SystemClock_Config()` plus `MX_*` peripheral init for FDCAN1, I2C1, ADC1, IWDG, UART2, and GPIO
- `Src/stm32g4xx_hal_msp.c`: MSP clock and pin mux glue
- `Src/stm32g4xx_it.c`: Cortex and optional peripheral ISR glue
- `Src/system_stm32g4xx.c`: minimal `SystemInit()`

What still needs the real STM32 vendor pack:

- `stm32g4xx_hal.h`, `stm32g474xx.h`, and the rest of the HAL/CMSIS driver tree from STM32Cube
- the final clock/peripheral values from the actual CubeMX project once the board file is regenerated on top of the real schematic

The code here is intentionally arranged so Cube-generated content stays boxed into this subtree and the rest of the firmware workspace remains portable.
