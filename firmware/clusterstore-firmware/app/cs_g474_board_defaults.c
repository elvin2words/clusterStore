#include "cs_g474_board_defaults.h"
#include "main.h"
#include <stddef.h>
#include <string.h>

void cs_g474_fill_default_bsp_config(cs_bsp_g474_config_t *config,
                                     const cs_cube_g474_board_handles_t *handles) {
    if (config == NULL || handles == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->precharge_drive.port = PRECHARGE_DRV_GPIO_Port;
    config->precharge_drive.pin = PRECHARGE_DRV_Pin;
    config->precharge_drive.active_high = true;
    config->main_contactor_drive.port = MAIN_CONTACTOR_DRV_GPIO_Port;
    config->main_contactor_drive.pin = MAIN_CONTACTOR_DRV_Pin;
    config->main_contactor_drive.active_high = true;
    config->precharge_feedback.port = PRECHARGE_FB_GPIO_Port;
    config->precharge_feedback.pin = PRECHARGE_FB_Pin;
    config->precharge_feedback.active_high = true;
    config->main_contactor_feedback.port = MAIN_CONTACTOR_FB_GPIO_Port;
    config->main_contactor_feedback.pin = MAIN_CONTACTOR_FB_Pin;
    config->main_contactor_feedback.active_high = true;
    config->can.hfdcan = handles->hfdcan1;
    config->can.kernel_clock_hz = CS_G474_FDCAN_DEFAULT_KERNEL_CLOCK_HZ;
    config->can.nominal_bitrate = CS_G474_FDCAN_DEFAULT_NOMINAL_BITRATE;
    config->can.enable_rx_fifo0_irq = 0U;
    config->flash.flash_base_address = CS_G474_FLASH_BASE_ADDRESS;
    config->flash.flash_size_bytes = CS_G474_FLASH_SIZE_BYTES;
    config->flash.page_size_bytes = CS_G474_FLASH_PAGE_SIZE_BYTES;
    config->flash.bank_size_bytes = CS_G474_FLASH_BANK_SIZE_BYTES;
    config->flash.bank_count = CS_G474_FLASH_BANK_COUNT;
    config->ina228.hi2c = handles->hi2c1;
    config->ina228.address_7bit = CS_INA228_DEFAULT_I2C_ADDRESS_7BIT;
    config->ina228.timeout_ms = 20U;
    config->ina228.current_lsb_ua = 2500U;
    config->ina228.apply_configuration_on_init = 0U;
    config->adc.hadc = handles->hadc1;
    config->adc.timeout_ms = 10U;
    config->adc.sampling_time = ADC_SAMPLETIME_47CYCLES_5;
    config->adc.resolution_counts = 4095U;
    config->adc.vref_mv = 3300U;
    config->adc.pack_voltage_channel = ADC_CHANNEL_1;
    config->adc.bus_voltage_channel = ADC_CHANNEL_2;
    config->adc.ntc_temperature_channel = ADC_CHANNEL_3;
    config->adc.pack_divider_top_ohms = 1000000U;
    config->adc.pack_divider_bottom_ohms = 10000U;
    config->adc.bus_divider_top_ohms = 1000000U;
    config->adc.bus_divider_bottom_ohms = 10000U;
    config->adc.ntc_series_resistor_ohms = 10000U;
    config->adc.ntc_nominal_resistor_ohms = 10000U;
    config->adc.ntc_nominal_temp_deci_c = 250;
    config->adc.ntc_beta = 3950U;
    config->iwdg.hiwdg = handles->hiwdg;
    config->iwdg.auto_start = 0U;
}
