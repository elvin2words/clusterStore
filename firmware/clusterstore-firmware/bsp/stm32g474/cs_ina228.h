#ifndef CS_INA228_H
#define CS_INA228_H

#include <stdint.h>
#include "cs_cluster_platform.h"

#define CS_INA228_DEFAULT_I2C_ADDRESS_7BIT 0x40U

typedef struct {
    void *hi2c;
    uint8_t address_7bit;
    uint32_t timeout_ms;
    uint16_t config_register;
    uint16_t adc_config_register;
    uint16_t shunt_calibration_register;
    uint16_t shunt_tempco_register;
    uint32_t current_lsb_ua;
    uint8_t apply_configuration_on_init;
} cs_g474_ina228_config_t;

typedef struct {
    cs_g474_ina228_config_t config;
    uint8_t configured;
} cs_g474_ina228_t;

int32_t cs_ina228_current_ma_from_raw(int32_t raw_current, uint32_t current_lsb_ua);
cs_status_t cs_ina228_init(cs_g474_ina228_t *device,
                           const cs_g474_ina228_config_t *config);
cs_status_t cs_ina228_apply_configuration(cs_g474_ina228_t *device);
cs_status_t cs_ina228_read_current_ma(cs_g474_ina228_t *device,
                                      int32_t *current_ma);

#endif
