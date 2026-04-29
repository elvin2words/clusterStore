#ifndef CS_CUBE_G474_BOARD_H
#define CS_CUBE_G474_BOARD_H

#include "main.h"

typedef struct {
    FDCAN_HandleTypeDef *hfdcan1;
    I2C_HandleTypeDef *hi2c1;
    ADC_HandleTypeDef *hadc1;
    IWDG_HandleTypeDef *hiwdg;
    UART_HandleTypeDef *huart2;
} cs_cube_g474_board_handles_t;

void cs_cube_g474_board_init(void);
const cs_cube_g474_board_handles_t *cs_cube_g474_board_handles(void);
void cs_cube_g474_on_fdcan_rx_fifo0_irq(void);
void SystemClock_Config(void);
void MX_GPIO_Init(void);
void MX_FDCAN1_Init(void);
void MX_I2C1_Init(void);
void MX_ADC1_Init(void);
void MX_IWDG_Init(void);
void MX_USART2_UART_Init(void);

#endif
