#include "cs_cube_g474_board.h"

FDCAN_HandleTypeDef hfdcan1;
I2C_HandleTypeDef hi2c1;
ADC_HandleTypeDef hadc1;
IWDG_HandleTypeDef hiwdg;
UART_HandleTypeDef huart2;

static cs_cube_g474_board_handles_t g_board_handles = {
    &hfdcan1,
    &hi2c1,
    &hadc1,
    &hiwdg,
    &huart2,
};

void Error_Handler(void) {
    __disable_irq();
    while (1) {
    }
}

__weak void cs_cube_g474_on_fdcan_rx_fifo0_irq(void) {
}

void cs_cube_g474_board_init(void) {
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_FDCAN1_Init();
    MX_I2C1_Init();
    MX_ADC1_Init();
    MX_IWDG_Init();
    MX_USART2_UART_Init();
}

const cs_cube_g474_board_handles_t *cs_cube_g474_board_handles(void) {
    return &g_board_handles;
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef osc_init;
    RCC_ClkInitTypeDef clock_init;
    RCC_PeriphCLKInitTypeDef peripheral_clock_init;

    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc_init.HSIState = RCC_HSI_ON;
    osc_init.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc_init.PLL.PLLState = RCC_PLL_ON;
    osc_init.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc_init.PLL.PLLM = RCC_PLLM_DIV4;
    osc_init.PLL.PLLN = 40U;
    osc_init.PLL.PLLP = RCC_PLLP_DIV2;
    osc_init.PLL.PLLQ = RCC_PLLQ_DIV2;
    osc_init.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&osc_init) != HAL_OK) {
        Error_Handler();
    }

    clock_init.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                           RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clock_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock_init.APB1CLKDivider = RCC_HCLK_DIV1;
    clock_init.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clock_init, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }

    peripheral_clock_init.PeriphClockSelection =
        RCC_PERIPHCLK_FDCAN | RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_ADC12 |
        RCC_PERIPHCLK_USART2;
    peripheral_clock_init.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    peripheral_clock_init.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    peripheral_clock_init.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
    peripheral_clock_init.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&peripheral_clock_init) != HAL_OK) {
        Error_Handler();
    }
}

void MX_GPIO_Init(void) {
    GPIO_InitTypeDef gpio_init;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(PRECHARGE_DRV_GPIO_Port, PRECHARGE_DRV_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MAIN_CONTACTOR_DRV_GPIO_Port,
                      MAIN_CONTACTOR_DRV_Pin,
                      GPIO_PIN_RESET);

    gpio_init.Pin = PRECHARGE_DRV_Pin | MAIN_CONTACTOR_DRV_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio_init);

    gpio_init.Pin = PRECHARGE_FB_Pin | MAIN_CONTACTOR_FB_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio_init);
}

void MX_FDCAN1_Init(void) {
    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan1.Init.AutoRetransmission = ENABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;
    hfdcan1.Init.NominalPrescaler = 10U;
    hfdcan1.Init.NominalSyncJumpWidth = 2U;
    hfdcan1.Init.NominalTimeSeg1 = 13U;
    hfdcan1.Init.NominalTimeSeg2 = 2U;
    hfdcan1.Init.DataPrescaler = 1U;
    hfdcan1.Init.DataSyncJumpWidth = 1U;
    hfdcan1.Init.DataTimeSeg1 = 1U;
    hfdcan1.Init.DataTimeSeg2 = 1U;
    hfdcan1.Init.StdFiltersNbr = 1U;
    hfdcan1.Init.ExtFiltersNbr = 1U;
    hfdcan1.Init.RxFifo0ElmtsNbr = 16U;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 16U;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        Error_Handler();
    }
}

void MX_I2C1_Init(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.Timing = 0x10707DBC;
    hi2c1.Init.OwnAddress1 = 0U;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0U;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK ||
        HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0U) != HAL_OK) {
        Error_Handler();
    }
}

void MX_ADC1_Init(void) {
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.GainCompensation = 0U;
    hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait = DISABLE;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1U;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        Error_Handler();
    }
}

void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    hiwdg.Init.Window = 4095U;
    hiwdg.Init.Reload = 2000U;
    if (HAL_IWDG_Init(&hiwdg) != HAL_OK) {
        Error_Handler();
    }
}

void MX_USART2_UART_Init(void) {
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200U;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *handle, uint32_t flags) {
    if (handle == &hfdcan1 && (flags & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0U) {
        cs_cube_g474_on_fdcan_rx_fifo0_irq();
    }
}
