#ifndef CS_ADC_G474_H
#define CS_ADC_G474_H

#include <stdint.h>
#include "cs_cluster_platform.h"

typedef struct {
    void *hadc;
    uint32_t timeout_ms;
    uint32_t sampling_time;
    uint32_t resolution_counts;
    uint32_t vref_mv;
    uint32_t pack_voltage_channel;
    uint32_t bus_voltage_channel;
    uint32_t ntc_temperature_channel;
    uint32_t pack_divider_top_ohms;
    uint32_t pack_divider_bottom_ohms;
    uint32_t bus_divider_top_ohms;
    uint32_t bus_divider_bottom_ohms;
    uint32_t ntc_series_resistor_ohms;
    uint32_t ntc_nominal_resistor_ohms;
    int32_t ntc_nominal_temp_deci_c;
    uint32_t ntc_beta;
} cs_g474_adc_config_t;

typedef struct {
    cs_g474_adc_config_t config;
} cs_g474_adc_t;

typedef struct {
    uint16_t pack_voltage_raw;
    uint16_t bus_voltage_raw;
    uint16_t ntc_temperature_raw;
    uint32_t pack_voltage_mv;
    uint32_t bus_voltage_mv;
    int32_t temperature_deci_c;
} cs_g474_adc_sample_t;

uint32_t cs_adc_g474_scale_divider_mv_from_raw(uint16_t raw_value,
                                               uint32_t resolution_counts,
                                               uint32_t vref_mv,
                                               uint32_t top_ohms,
                                               uint32_t bottom_ohms);
int32_t cs_adc_g474_ntc_temperature_deci_c_from_raw(
    uint16_t raw_value,
    uint32_t resolution_counts,
    uint32_t series_resistor_ohms,
    uint32_t nominal_resistor_ohms,
    int32_t nominal_temp_deci_c,
    uint32_t beta);
cs_status_t cs_adc_g474_init(cs_g474_adc_t *adc,
                             const cs_g474_adc_config_t *config);
cs_status_t cs_adc_g474_read_all(cs_g474_adc_t *adc,
                                 cs_g474_adc_sample_t *sample);

#endif
