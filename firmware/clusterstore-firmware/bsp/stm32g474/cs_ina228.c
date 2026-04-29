#include "cs_ina228.h"
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

#define CS_INA228_REG_CONFIG 0x00U
#define CS_INA228_REG_ADC_CONFIG 0x01U
#define CS_INA228_REG_SHUNT_CAL 0x02U
#define CS_INA228_REG_SHUNT_TEMPCO 0x03U
#define CS_INA228_REG_CURRENT 0x07U

#ifdef CS_G474_USE_HAL
static cs_status_t cs_ina228_write_u16(cs_g474_ina228_t *device,
                                       uint8_t reg,
                                       uint16_t value) {
    uint8_t buffer[2];

    buffer[0] = (uint8_t)(value >> 8U);
    buffer[1] = (uint8_t)(value & 0xFFU);
    return HAL_I2C_Mem_Write((I2C_HandleTypeDef *)device->config.hi2c,
                             (uint16_t)(device->config.address_7bit << 1U),
                             reg,
                             I2C_MEMADD_SIZE_8BIT,
                             buffer,
                             sizeof(buffer),
                             device->config.timeout_ms) == HAL_OK
               ? CS_STATUS_OK
               : CS_STATUS_ERROR;
}

static cs_status_t cs_ina228_read_s24(cs_g474_ina228_t *device,
                                      uint8_t reg,
                                      int32_t *value) {
    uint8_t buffer[3];
    int32_t signed_value;

    if (value == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (HAL_I2C_Mem_Read((I2C_HandleTypeDef *)device->config.hi2c,
                         (uint16_t)(device->config.address_7bit << 1U),
                         reg,
                         I2C_MEMADD_SIZE_8BIT,
                         buffer,
                         sizeof(buffer),
                         device->config.timeout_ms) != HAL_OK) {
        return CS_STATUS_ERROR;
    }

    signed_value = ((int32_t)buffer[0] << 16U) |
                   ((int32_t)buffer[1] << 8U) |
                   (int32_t)buffer[2];
    if ((signed_value & 0x00800000L) != 0L) {
        signed_value |= (int32_t)0xFF000000L;
    }

    *value = signed_value;
    return CS_STATUS_OK;
}
#endif

cs_status_t cs_ina228_init(cs_g474_ina228_t *device,
                           const cs_g474_ina228_config_t *config) {
    if (device == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(device, 0, sizeof(*device));
    device->config = *config;
    if (device->config.address_7bit == 0U) {
        device->config.address_7bit = CS_INA228_DEFAULT_I2C_ADDRESS_7BIT;
    }

    if (device->config.timeout_ms == 0U) {
        device->config.timeout_ms = 20U;
    }

    if (config->hi2c == NULL || config->current_lsb_ua == 0U) {
#ifdef CS_G474_USE_HAL
        return CS_STATUS_INVALID_ARGUMENT;
#else
        return CS_STATUS_UNSUPPORTED;
#endif
    }

    if (device->config.apply_configuration_on_init != 0U) {
        return cs_ina228_apply_configuration(device);
    }

    return CS_STATUS_OK;
}

int32_t cs_ina228_current_ma_from_raw(int32_t raw_current, uint32_t current_lsb_ua) {
    int64_t current_ua;

    if (current_lsb_ua == 0U) {
        return 0;
    }

    current_ua = (int64_t)raw_current * (int64_t)current_lsb_ua;
    return (int32_t)(current_ua / 1000LL);
}

cs_status_t cs_ina228_apply_configuration(cs_g474_ina228_t *device) {
    if (device == NULL || device->config.hi2c == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    if (cs_ina228_write_u16(device,
                            CS_INA228_REG_CONFIG,
                            device->config.config_register) != CS_STATUS_OK ||
        cs_ina228_write_u16(device,
                            CS_INA228_REG_ADC_CONFIG,
                            device->config.adc_config_register) != CS_STATUS_OK ||
        cs_ina228_write_u16(device,
                            CS_INA228_REG_SHUNT_CAL,
                            device->config.shunt_calibration_register) != CS_STATUS_OK ||
        cs_ina228_write_u16(device,
                            CS_INA228_REG_SHUNT_TEMPCO,
                            device->config.shunt_tempco_register) != CS_STATUS_OK) {
        return CS_STATUS_ERROR;
    }

    device->configured = 1U;
    return CS_STATUS_OK;
#else
    return CS_STATUS_UNSUPPORTED;
#endif
}

cs_status_t cs_ina228_read_current_ma(cs_g474_ina228_t *device,
                                      int32_t *current_ma) {
    if (device == NULL || current_ma == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    {
        int32_t raw_current;

        if (cs_ina228_read_s24(device, CS_INA228_REG_CURRENT, &raw_current) !=
            CS_STATUS_OK) {
            return CS_STATUS_ERROR;
        }

        *current_ma = cs_ina228_current_ma_from_raw(raw_current,
                                                    device->config.current_lsb_ua);
        return CS_STATUS_OK;
    }
#else
    (void)device;
    return CS_STATUS_UNSUPPORTED;
#endif
}
