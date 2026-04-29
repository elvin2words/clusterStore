#ifndef STM32G4XX_HAL_CONF_H
#define STM32G4XX_HAL_CONF_H

#define HAL_MODULE_ENABLED
#define HAL_ADC_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FDCAN_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
#define HAL_IWDG_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

#define HSE_VALUE 8000000U
#define HSE_STARTUP_TIMEOUT 100U
#define HSI_VALUE 16000000U
#define HSI48_VALUE 48000000U
#define LSE_VALUE 32768U
#define LSE_STARTUP_TIMEOUT 5000U
#define LSI_VALUE 32000U
#define EXTERNAL_CLOCK_VALUE 12288000U
#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0x0FU
#define USE_RTOS 0U
#define PREFETCH_ENABLE 1U

#include "stm32g4xx_hal_rcc.h"
#include "stm32g4xx_hal_rcc_ex.h"
#include "stm32g4xx_hal_gpio.h"
#include "stm32g4xx_hal_dma.h"
#include "stm32g4xx_hal_cortex.h"
#include "stm32g4xx_hal_adc.h"
#include "stm32g4xx_hal_adc_ex.h"
#include "stm32g4xx_hal_exti.h"
#include "stm32g4xx_hal_fdcan.h"
#include "stm32g4xx_hal_flash.h"
#include "stm32g4xx_hal_flash_ex.h"
#include "stm32g4xx_hal_i2c.h"
#include "stm32g4xx_hal_iwdg.h"
#include "stm32g4xx_hal_pwr.h"
#include "stm32g4xx_hal_pwr_ex.h"
#include "stm32g4xx_hal_uart.h"

#ifndef assert_param
#define assert_param(expr) ((void)0U)
#endif

#endif
