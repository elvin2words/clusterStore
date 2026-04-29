#include "cs_adc_g474.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

#ifdef CS_G474_USE_HAL
static cs_status_t cs_adc_g474_read_channel_raw(const cs_g474_adc_t *adc,
                                                uint32_t channel,
                                                uint16_t *raw_value) {
    ADC_ChannelConfTypeDef channel_config;

    if (adc == NULL || raw_value == NULL || adc->config.hadc == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(&channel_config, 0, sizeof(channel_config));
    channel_config.Channel = channel;
    channel_config.Rank = ADC_REGULAR_RANK_1;
    channel_config.SamplingTime = adc->config.sampling_time;
    channel_config.SingleDiff = ADC_SINGLE_ENDED;
    channel_config.OffsetNumber = ADC_OFFSET_NONE;
    channel_config.Offset = 0U;

    if (HAL_ADC_ConfigChannel((ADC_HandleTypeDef *)adc->config.hadc,
                              &channel_config) != HAL_OK ||
        HAL_ADC_Start((ADC_HandleTypeDef *)adc->config.hadc) != HAL_OK ||
        HAL_ADC_PollForConversion((ADC_HandleTypeDef *)adc->config.hadc,
                                  adc->config.timeout_ms) != HAL_OK) {
        return CS_STATUS_ERROR;
    }

    *raw_value = (uint16_t)HAL_ADC_GetValue((ADC_HandleTypeDef *)adc->config.hadc);
    HAL_ADC_Stop((ADC_HandleTypeDef *)adc->config.hadc);
    return CS_STATUS_OK;
}
#endif

uint32_t cs_adc_g474_scale_divider_mv_from_raw(uint16_t raw_value,
                                               uint32_t resolution_counts,
                                               uint32_t vref_mv,
                                               uint32_t top_ohms,
                                               uint32_t bottom_ohms) {
    uint64_t adc_mv;

    if (resolution_counts == 0U || bottom_ohms == 0U) {
        return 0U;
    }

    adc_mv = ((uint64_t)raw_value * (uint64_t)vref_mv) / (uint64_t)resolution_counts;
    return (uint32_t)((adc_mv * (uint64_t)(top_ohms + bottom_ohms)) /
                      (uint64_t)bottom_ohms);
}

int32_t cs_adc_g474_ntc_temperature_deci_c_from_raw(
    uint16_t raw_value,
    uint32_t resolution_counts,
    uint32_t series_resistor_ohms,
    uint32_t nominal_resistor_ohms,
    int32_t nominal_temp_deci_c,
    uint32_t beta) {
    double ratio;
    double ntc_resistance;
    double nominal_kelvin;
    double temperature_kelvin;
    double temperature_celsius;

    if (raw_value == 0U || raw_value >= resolution_counts ||
        series_resistor_ohms == 0U || nominal_resistor_ohms == 0U || beta == 0U) {
        return 0;
    }

    ratio = (double)raw_value / (double)(resolution_counts - raw_value);
    ntc_resistance = (double)series_resistor_ohms * ratio;
    nominal_kelvin = ((double)nominal_temp_deci_c / 10.0) + 273.15;
    temperature_kelvin =
        1.0 / ((1.0 / nominal_kelvin) +
               (log(ntc_resistance / (double)nominal_resistor_ohms) /
                (double)beta));
    temperature_celsius = temperature_kelvin - 273.15;
    return (int32_t)(temperature_celsius * 10.0);
}

cs_status_t cs_adc_g474_init(cs_g474_adc_t *adc,
                             const cs_g474_adc_config_t *config) {
    if (adc == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(adc, 0, sizeof(*adc));
    adc->config = *config;
    if (adc->config.timeout_ms == 0U) {
        adc->config.timeout_ms = 10U;
    }

    if (config->hadc == NULL || config->resolution_counts == 0U ||
        config->vref_mv == 0U) {
#ifdef CS_G474_USE_HAL
        return CS_STATUS_INVALID_ARGUMENT;
#else
        return CS_STATUS_UNSUPPORTED;
#endif
    }

    return CS_STATUS_OK;
}

cs_status_t cs_adc_g474_read_all(cs_g474_adc_t *adc,
                                 cs_g474_adc_sample_t *sample) {
    if (adc == NULL || sample == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    if (cs_adc_g474_read_channel_raw(adc,
                                     adc->config.pack_voltage_channel,
                                     &sample->pack_voltage_raw) != CS_STATUS_OK ||
        cs_adc_g474_read_channel_raw(adc,
                                     adc->config.bus_voltage_channel,
                                     &sample->bus_voltage_raw) != CS_STATUS_OK ||
        cs_adc_g474_read_channel_raw(adc,
                                     adc->config.ntc_temperature_channel,
                                     &sample->ntc_temperature_raw) != CS_STATUS_OK) {
        return CS_STATUS_ERROR;
    }

    sample->pack_voltage_mv = cs_adc_g474_scale_divider_mv_from_raw(
        sample->pack_voltage_raw,
        adc->config.resolution_counts,
        adc->config.vref_mv,
        adc->config.pack_divider_top_ohms,
        adc->config.pack_divider_bottom_ohms);
    sample->bus_voltage_mv = cs_adc_g474_scale_divider_mv_from_raw(
        sample->bus_voltage_raw,
        adc->config.resolution_counts,
        adc->config.vref_mv,
        adc->config.bus_divider_top_ohms,
        adc->config.bus_divider_bottom_ohms);
    sample->temperature_deci_c = cs_adc_g474_ntc_temperature_deci_c_from_raw(
        sample->ntc_temperature_raw,
        adc->config.resolution_counts,
        adc->config.ntc_series_resistor_ohms,
        adc->config.ntc_nominal_resistor_ohms,
        adc->config.ntc_nominal_temp_deci_c,
        adc->config.ntc_beta);
    return CS_STATUS_OK;
#else
    (void)sample;
    return CS_STATUS_UNSUPPORTED;
#endif
}
