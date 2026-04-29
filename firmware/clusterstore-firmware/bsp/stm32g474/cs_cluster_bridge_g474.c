#include "cs_cluster_bridge_g474.h"
#include "cluster_can_protocol.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

static cluster_platform_status_t cs_cluster_bridge_map_status(cs_status_t status) {
    switch (status) {
        case CS_STATUS_OK:
            return CLUSTER_PLATFORM_STATUS_OK;
        case CS_STATUS_TIMEOUT:
            return CLUSTER_PLATFORM_STATUS_TIMEOUT;
        case CS_STATUS_UNSUPPORTED:
            return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
        case CS_STATUS_INVALID_ARGUMENT:
            return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
        case CS_STATUS_ERROR:
        case CS_STATUS_NOT_FOUND:
        default:
            return CLUSTER_PLATFORM_STATUS_ERROR;
    }
}

static cluster_stm32_gpio_t cs_cluster_bridge_gpio(const cs_bsp_gpio_t *gpio) {
    cluster_stm32_gpio_t mapped_gpio;

    memset(&mapped_gpio, 0, sizeof(mapped_gpio));
    if (gpio == NULL) {
        return mapped_gpio;
    }

    mapped_gpio.port = gpio->port;
    mapped_gpio.pin = gpio->pin;
    mapped_gpio.active_high = gpio->active_high;
    return mapped_gpio;
}

static uint32_t cs_cluster_bridge_now_ms(void *context) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL) {
        return 0U;
    }

    return cs_platform_monotonic_ms(&bridge->bsp->platform);
}

static cluster_platform_status_t cs_cluster_bridge_can_send(
    void *context,
    const cluster_can_frame_t *frame) {
    cs_cluster_bridge_g474_t *bridge;
    cs_can_frame_t native_frame;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL || frame == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    memset(&native_frame, 0, sizeof(native_frame));
    native_frame.id = frame->id;
    native_frame.dlc = frame->dlc;
    memcpy(native_frame.data, frame->data, sizeof(native_frame.data));
    return cs_cluster_bridge_map_status(
        cs_platform_can_send(&bridge->bsp->platform, &native_frame));
}

static bool cs_cluster_bridge_can_receive(void *context, cluster_can_frame_t *frame) {
    cs_cluster_bridge_g474_t *bridge;
    cs_can_frame_t native_frame;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL || frame == NULL) {
        return false;
    }

    if (!cs_platform_can_receive(&bridge->bsp->platform, &native_frame)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->id = native_frame.id;
    frame->dlc = native_frame.dlc;
    memcpy(frame->data, native_frame.data, sizeof(frame->data));
    return true;
}

static cluster_platform_status_t cs_cluster_bridge_sample_measurements(
    void *context,
    cluster_node_measurements_t *measurements) {
    cs_cluster_bridge_g474_t *bridge;
    cs_bsp_measurements_t native_measurements;
    cs_status_t status;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL || measurements == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    memset(&native_measurements, 0, sizeof(native_measurements));
    status = cs_bsp_g474_sample_measurements(bridge->bsp, &native_measurements);
    if (status != CS_STATUS_OK) {
        return cs_cluster_bridge_map_status(status);
    }

    memset(measurements, 0, sizeof(*measurements));
    measurements->timestamp_ms = native_measurements.timestamp_ms;
    measurements->soc_pct = bridge->config.default_soc_pct;
    measurements->pack_voltage_mv =
        native_measurements.pack_voltage_mv > UINT16_MAX
            ? UINT16_MAX
            : (uint16_t)native_measurements.pack_voltage_mv;
    measurements->bus_voltage_mv =
        native_measurements.bus_voltage_mv > UINT16_MAX
            ? UINT16_MAX
            : (uint16_t)native_measurements.bus_voltage_mv;
    measurements->measured_current_da =
        native_measurements.current_ma > INT16_MAX * 100
            ? INT16_MAX
            : native_measurements.current_ma < INT16_MIN * 100
                  ? INT16_MIN
                  : (int16_t)(native_measurements.current_ma / 100);
    measurements->temperature_c =
        native_measurements.temperature_deci_c > INT8_MAX * 10
            ? INT8_MAX
            : native_measurements.temperature_deci_c < INT8_MIN * 10
                  ? INT8_MIN
                  : (int8_t)(native_measurements.temperature_deci_c / 10);
    measurements->local_fault_flags = bridge->config.local_fault_flags;
    measurements->balancing_supported = bridge->config.balancing_supported;
    measurements->maintenance_lockout = bridge->config.maintenance_lockout;
    measurements->service_lockout = bridge->config.service_lockout;
    return CLUSTER_PLATFORM_STATUS_OK;
}

static cluster_platform_status_t cs_cluster_bridge_flash_read(void *context,
                                                              uint32_t address,
                                                              void *buffer,
                                                              uint32_t length) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return cs_cluster_bridge_map_status(
        cs_platform_flash_read(&bridge->bsp->platform, address, buffer, length));
}

static cluster_platform_status_t cs_cluster_bridge_flash_write(void *context,
                                                               uint32_t address,
                                                               const void *data,
                                                               uint32_t length) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return cs_cluster_bridge_map_status(
        cs_platform_flash_write(&bridge->bsp->platform, address, data, length));
}

static cluster_platform_status_t cs_cluster_bridge_flash_erase(void *context,
                                                               uint32_t address,
                                                               uint32_t length) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return cs_cluster_bridge_map_status(
        cs_platform_flash_erase(&bridge->bsp->platform, address, length));
}

static cluster_platform_status_t cs_cluster_bridge_schedule_boot_slot(void *context,
                                                                      uint8_t slot_id) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    bridge->scheduled_boot_slot = slot_id;
    return CLUSTER_PLATFORM_STATUS_OK;
}

static cluster_platform_status_t cs_cluster_bridge_watchdog_kick(void *context) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL || bridge->bsp == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return cs_cluster_bridge_map_status(cs_bsp_g474_watchdog_kick(bridge->bsp));
}

static void cs_cluster_bridge_request_reset(void *context) {
    cs_cluster_bridge_g474_t *bridge;

    bridge = (cs_cluster_bridge_g474_t *)context;
    if (bridge == NULL) {
        return;
    }

    bridge->reset_requested = true;
#ifdef CS_G474_USE_HAL
    HAL_NVIC_SystemReset();
#endif
}

cs_status_t cs_cluster_bridge_g474_init(cs_cluster_bridge_g474_t *bridge,
                                        cs_bsp_g474_t *bsp,
                                        const cs_cluster_bridge_g474_config_t *config) {
    cluster_stm32_hal_config_t hal_config;

    if (bridge == NULL || bsp == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(bridge, 0, sizeof(*bridge));
    bridge->bsp = bsp;
    bridge->config = *config;

    memset(&hal_config, 0, sizeof(hal_config));
    hal_config.user_context = bridge;
    hal_config.now_ms = cs_cluster_bridge_now_ms;
    hal_config.can_send = cs_cluster_bridge_can_send;
    hal_config.can_receive = cs_cluster_bridge_can_receive;
    hal_config.sample_measurements = cs_cluster_bridge_sample_measurements;
    hal_config.flash_read = cs_cluster_bridge_flash_read;
    hal_config.flash_write = cs_cluster_bridge_flash_write;
    hal_config.flash_erase = cs_cluster_bridge_flash_erase;
    hal_config.schedule_boot_slot = cs_cluster_bridge_schedule_boot_slot;
    hal_config.watchdog_kick = cs_cluster_bridge_watchdog_kick;
    hal_config.request_reset = cs_cluster_bridge_request_reset;
    hal_config.precharge_drive = cs_cluster_bridge_gpio(&bsp->config.precharge_drive);
    hal_config.main_contactor_drive =
        cs_cluster_bridge_gpio(&bsp->config.main_contactor_drive);
    hal_config.precharge_feedback =
        cs_cluster_bridge_gpio(&bsp->config.precharge_feedback);
    hal_config.main_contactor_feedback =
        cs_cluster_bridge_gpio(&bsp->config.main_contactor_feedback);
    hal_config.can_bus.handle = bsp->can.hfdcan;
    hal_config.can_bus.backend = CLUSTER_STM32_CAN_BACKEND_FDCAN;
#ifdef CS_G474_USE_HAL
    hal_config.can_bus.tx_identifier_type = FDCAN_STANDARD_ID;
#endif
    hal_config.can_bus.rx_fifo_index = 0U;
    hal_config.watchdog_handle = bsp->iwdg.config.hiwdg;
    cluster_stm32_hal_port_init(&bridge->hal_port, &hal_config);
    cluster_stm32_hal_bind_platform(&bridge->hal_port, &bridge->cluster_platform);
    return CS_STATUS_OK;
}

void cs_cluster_bridge_g474_bind_platform(cs_cluster_bridge_g474_t *bridge,
                                          cluster_platform_t *platform) {
    if (bridge == NULL || platform == NULL) {
        return;
    }

    *platform = bridge->cluster_platform;
}

uint8_t cs_cluster_bridge_g474_scheduled_boot_slot(
    const cs_cluster_bridge_g474_t *bridge) {
    return bridge == NULL ? 0U : bridge->scheduled_boot_slot;
}

bool cs_cluster_bridge_g474_reset_requested(const cs_cluster_bridge_g474_t *bridge) {
    return bridge != NULL && bridge->reset_requested;
}
