#include "cs_bsp_g474.h"
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

static uint32_t cs_bsp_g474_now_ms(void *context) {
    cs_bsp_g474_t *bsp;

    bsp = (cs_bsp_g474_t *)context;
    if (bsp->config.now_ms != NULL) {
        return bsp->config.now_ms(bsp->config.time_context);
    }

#ifdef CS_G474_USE_HAL
    return HAL_GetTick();
#else
    return 0U;
#endif
}

static void cs_bsp_g474_log_write(void *context,
                                  cs_log_level_t level,
                                  const char *message) {
    cs_bsp_g474_t *bsp;

    bsp = (cs_bsp_g474_t *)context;
    if (bsp->config.log_write != NULL) {
        bsp->config.log_write(bsp->config.log_context, level, message);
    }
}

static cs_status_t cs_bsp_g474_write_gpio(const cs_bsp_gpio_t *gpio,
                                          bool enabled) {
    if (gpio == NULL || gpio->port == NULL) {
        return CS_STATUS_UNSUPPORTED;
    }

#ifdef CS_G474_USE_HAL
    HAL_GPIO_WritePin((GPIO_TypeDef *)gpio->port,
                      gpio->pin,
                      enabled == gpio->active_high ? GPIO_PIN_SET
                                                   : GPIO_PIN_RESET);
    return CS_STATUS_OK;
#else
    (void)enabled;
    return CS_STATUS_UNSUPPORTED;
#endif
}

static bool cs_bsp_g474_read_gpio(const cs_bsp_gpio_t *gpio) {
    if (gpio == NULL || gpio->port == NULL) {
        return false;
    }

#ifdef CS_G474_USE_HAL
    return (HAL_GPIO_ReadPin((GPIO_TypeDef *)gpio->port, gpio->pin) ==
            GPIO_PIN_SET) == gpio->active_high;
#else
    return false;
#endif
}

cs_status_t cs_bsp_g474_init(cs_bsp_g474_t *bsp,
                             const cs_bsp_g474_config_t *config) {
    cs_status_t status;

    if (bsp == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(bsp, 0, sizeof(*bsp));
    bsp->config = *config;
    cs_platform_init(&bsp->platform);

    status = cs_can_g474_init(&bsp->can, &config->can);
    if (status != CS_STATUS_OK) {
        return status;
    }

    status = cs_flash_g474_init(&bsp->flash, &config->flash);
    if (status != CS_STATUS_OK) {
        return status;
    }

    status = cs_ina228_init(&bsp->ina228, &config->ina228);
    if (status != CS_STATUS_OK && status != CS_STATUS_UNSUPPORTED) {
        return status;
    }

    status = cs_adc_g474_init(&bsp->adc, &config->adc);
    if (status != CS_STATUS_OK && status != CS_STATUS_UNSUPPORTED) {
        return status;
    }

    status = cs_iwdg_g474_init(&bsp->iwdg, &config->iwdg);
    if (status != CS_STATUS_OK && status != CS_STATUS_UNSUPPORTED) {
        return status;
    }

    cs_bsp_g474_bind_platform(bsp, &bsp->platform);
    return CS_STATUS_OK;
}

void cs_bsp_g474_bind_platform(cs_bsp_g474_t *bsp, cs_platform_t *platform) {
    if (bsp == NULL || platform == NULL) {
        return;
    }

    cs_platform_init(platform);
    platform->flash.context = &bsp->flash;
    platform->flash.page_size_bytes = bsp->flash.config.page_size_bytes;
    platform->flash.read = cs_flash_g474_read;
    platform->flash.write = cs_flash_g474_write;
    platform->flash.erase = cs_flash_g474_erase;
    platform->can.context = &bsp->can;
    platform->can.send = cs_can_g474_send;
    platform->can.receive = cs_can_g474_receive;
    platform->time.context = bsp;
    platform->time.monotonic_ms = cs_bsp_g474_now_ms;
    platform->log.context = bsp;
    platform->log.write = cs_bsp_g474_log_write;
}

cs_status_t cs_bsp_g474_start_can(cs_bsp_g474_t *bsp) {
    if (bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return cs_can_g474_start(&bsp->can);
}

void cs_bsp_g474_on_fdcan_rx_fifo0_irq(cs_bsp_g474_t *bsp) {
    if (bsp == NULL) {
        return;
    }

    (void)cs_can_g474_drain_rx_fifo0(&bsp->can);
}

cs_status_t cs_bsp_g474_sample_measurements(cs_bsp_g474_t *bsp,
                                            cs_bsp_measurements_t *measurements) {
    cs_g474_adc_sample_t adc_sample;
    int32_t current_ma;
    cs_status_t adc_status;
    cs_status_t current_status;

    if (bsp == NULL || measurements == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (bsp->config.sample_measurements != NULL) {
        return bsp->config.sample_measurements(bsp->config.measurement_context,
                                               measurements);
    }

    memset(measurements, 0, sizeof(*measurements));
    measurements->timestamp_ms = cs_platform_monotonic_ms(&bsp->platform);

    memset(&adc_sample, 0, sizeof(adc_sample));
    adc_status = cs_adc_g474_read_all(&bsp->adc, &adc_sample);
    if (adc_status == CS_STATUS_OK) {
        measurements->pack_voltage_mv = adc_sample.pack_voltage_mv;
        measurements->bus_voltage_mv = adc_sample.bus_voltage_mv;
        measurements->temperature_deci_c = adc_sample.temperature_deci_c;
        measurements->voltages_valid = true;
        measurements->temperature_valid = true;
    }

    current_status = cs_ina228_read_current_ma(&bsp->ina228, &current_ma);
    if (current_status == CS_STATUS_OK) {
        measurements->current_ma = current_ma;
        measurements->current_valid = true;
    }

    if (adc_status == CS_STATUS_OK || current_status == CS_STATUS_OK) {
        return CS_STATUS_OK;
    }

    return adc_status != CS_STATUS_OK ? adc_status : current_status;
}

cs_status_t cs_bsp_g474_watchdog_kick(cs_bsp_g474_t *bsp) {
    if (bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (bsp->config.watchdog_kick != NULL) {
        return bsp->config.watchdog_kick(bsp->config.watchdog_context);
    }

    return cs_iwdg_g474_kick(&bsp->iwdg);
}

cs_status_t cs_bsp_g474_set_precharge_drive(cs_bsp_g474_t *bsp, bool enabled) {
    if (bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return cs_bsp_g474_write_gpio(&bsp->config.precharge_drive, enabled);
}

cs_status_t cs_bsp_g474_set_main_contactor_drive(cs_bsp_g474_t *bsp,
                                                 bool enabled) {
    if (bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return cs_bsp_g474_write_gpio(&bsp->config.main_contactor_drive, enabled);
}

bool cs_bsp_g474_read_precharge_feedback(const cs_bsp_g474_t *bsp) {
    if (bsp == NULL) {
        return false;
    }

    return cs_bsp_g474_read_gpio(&bsp->config.precharge_feedback);
}

bool cs_bsp_g474_read_main_contactor_feedback(const cs_bsp_g474_t *bsp) {
    if (bsp == NULL) {
        return false;
    }

    return cs_bsp_g474_read_gpio(&bsp->config.main_contactor_feedback);
}
