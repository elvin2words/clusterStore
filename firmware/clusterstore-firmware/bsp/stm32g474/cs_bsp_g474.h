#ifndef CS_BSP_G474_H
#define CS_BSP_G474_H

#include "cs_adc_g474.h"
#include "cs_bsp.h"
#include "cs_can_g474.h"
#include "cs_flash_g474.h"
#include "cs_ina228.h"
#include "cs_iwdg_g474.h"

#define CS_G474_BOOTLOADER_ADDRESS 0x08000000UL
#define CS_G474_BCB_A_ADDRESS 0x08008000UL
#define CS_G474_BCB_B_ADDRESS 0x08009000UL
#define CS_G474_JOURNAL_ADDRESS 0x0800A000UL
#define CS_G474_JOURNAL_META_A_ADDRESS 0x0800A000UL
#define CS_G474_JOURNAL_META_B_ADDRESS 0x0800A800UL
#define CS_G474_JOURNAL_RECORD_AREA_ADDRESS 0x0800B000UL
#define CS_G474_SLOT_A_ADDRESS 0x08010000UL
#define CS_G474_SLOT_B_ADDRESS 0x08040000UL
#define CS_G474_RESERVED_ADDRESS 0x08070000UL

typedef struct {
    void *time_context;
    uint32_t (*now_ms)(void *context);
    void *log_context;
    void (*log_write)(void *context, cs_log_level_t level, const char *message);
    void *measurement_context;
    cs_status_t (*sample_measurements)(void *context,
                                       cs_bsp_measurements_t *measurements);
    void *watchdog_context;
    cs_status_t (*watchdog_kick)(void *context);
    cs_bsp_gpio_t precharge_drive;
    cs_bsp_gpio_t main_contactor_drive;
    cs_bsp_gpio_t precharge_feedback;
    cs_bsp_gpio_t main_contactor_feedback;
    cs_g474_can_config_t can;
    cs_g474_flash_config_t flash;
    cs_g474_ina228_config_t ina228;
    cs_g474_adc_config_t adc;
    cs_g474_iwdg_config_t iwdg;
} cs_bsp_g474_config_t;

typedef struct {
    cs_platform_t platform;
    cs_g474_can_t can;
    cs_g474_flash_t flash;
    cs_g474_ina228_t ina228;
    cs_g474_adc_t adc;
    cs_g474_iwdg_t iwdg;
    cs_bsp_g474_config_t config;
} cs_bsp_g474_t;

cs_status_t cs_bsp_g474_init(cs_bsp_g474_t *bsp,
                             const cs_bsp_g474_config_t *config);
void cs_bsp_g474_bind_platform(cs_bsp_g474_t *bsp, cs_platform_t *platform);
cs_status_t cs_bsp_g474_start_can(cs_bsp_g474_t *bsp);
void cs_bsp_g474_on_fdcan_rx_fifo0_irq(cs_bsp_g474_t *bsp);
cs_status_t cs_bsp_g474_sample_measurements(cs_bsp_g474_t *bsp,
                                            cs_bsp_measurements_t *measurements);
cs_status_t cs_bsp_g474_watchdog_kick(cs_bsp_g474_t *bsp);
cs_status_t cs_bsp_g474_set_precharge_drive(cs_bsp_g474_t *bsp, bool enabled);
cs_status_t cs_bsp_g474_set_main_contactor_drive(cs_bsp_g474_t *bsp,
                                                 bool enabled);
bool cs_bsp_g474_read_precharge_feedback(const cs_bsp_g474_t *bsp);
bool cs_bsp_g474_read_main_contactor_feedback(const cs_bsp_g474_t *bsp);

#endif
