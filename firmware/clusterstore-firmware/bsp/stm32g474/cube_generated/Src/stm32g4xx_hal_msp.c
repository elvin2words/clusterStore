#include "main.h"

void HAL_MspInit(void) {
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_PWR_CLK_ENABLE();
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *handle) {
    GPIO_InitTypeDef gpio_init;

    if (handle->Instance != FDCAN1) {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_FDCAN_CLK_ENABLE();
    gpio_init.Pin = FDCAN1_RX_Pin | FDCAN1_TX_Pin;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF9_FDCAN1;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    HAL_NVIC_SetPriority(FDCAN1_IT0_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *handle) {
    GPIO_InitTypeDef gpio_init;

    if (handle->Instance != I2C1) {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    gpio_init.Pin = INA228_SCL_Pin | INA228_SDA_Pin;
    gpio_init.Mode = GPIO_MODE_AF_OD;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio_init);
}

void HAL_ADC_MspInit(ADC_HandleTypeDef *handle) {
    GPIO_InitTypeDef gpio_init;

    if (handle->Instance != ADC1) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC12_CLK_ENABLE();
    gpio_init.Pin = PACK_VOLTAGE_ADC_Pin | BUS_VOLTAGE_ADC_Pin | NTC_TEMPERATURE_ADC_Pin;
    gpio_init.Mode = GPIO_MODE_ANALOG;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle) {
    GPIO_InitTypeDef gpio_init;

    if (handle->Instance != USART2) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    gpio_init.Pin = DEBUG_UART_TX_Pin | DEBUG_UART_RX_Pin;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio_init);
}
