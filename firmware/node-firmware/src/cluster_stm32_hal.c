#include "cluster_stm32_hal.h"
#include <stddef.h>
#include <string.h>

#ifdef CLUSTER_STM32_HAL_HEADER
#include CLUSTER_STM32_HAL_HEADER
#endif

static cluster_platform_status_t cluster_stm32_map_hal_status(int hal_status) {
    return hal_status == 0 ? CLUSTER_PLATFORM_STATUS_OK
                           : CLUSTER_PLATFORM_STATUS_ERROR;
}

static uint32_t cluster_stm32_now_ms(void *context) {
    cluster_stm32_hal_port_t *port;

    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.now_ms != NULL) {
        return port->config.now_ms(port->config.user_context);
    }

#ifdef CLUSTER_STM32_HAL_HEADER
    return HAL_GetTick();
#else
    return 0U;
#endif
}

static cluster_platform_status_t cluster_stm32_can_send(void *context,
                                                        const cluster_can_frame_t *frame) {
    cluster_stm32_hal_port_t *port;

    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.can_send != NULL) {
        return port->config.can_send(port->config.user_context, frame);
    }

#if defined(HAL_CAN_MODULE_ENABLED)
    if (port->config.can_bus.backend == CLUSTER_STM32_CAN_BACKEND_BXCAN &&
        port->config.can_bus.handle != NULL) {
        CAN_TxHeaderTypeDef header;
        uint32_t mailbox;

        memset(&header, 0, sizeof(header));
        header.StdId = frame->id & 0x7FFU;
        header.IDE = CAN_ID_STD;
        header.RTR = CAN_RTR_DATA;
        header.DLC = frame->dlc;
        return cluster_stm32_map_hal_status(HAL_CAN_AddTxMessage(
            (CAN_HandleTypeDef *)port->config.can_bus.handle,
            &header,
            (uint8_t *)frame->data,
            &mailbox));
    }
#endif

#if defined(HAL_FDCAN_MODULE_ENABLED)
    if (port->config.can_bus.backend == CLUSTER_STM32_CAN_BACKEND_FDCAN &&
        port->config.can_bus.handle != NULL) {
        FDCAN_TxHeaderTypeDef header;

        memset(&header, 0, sizeof(header));
        header.Identifier = frame->id;
        header.IdType = port->config.can_bus.tx_identifier_type;
        header.TxFrameType = FDCAN_DATA_FRAME;
        header.DataLength = (uint32_t)frame->dlc << 16U;
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0U;
        return cluster_stm32_map_hal_status(HAL_FDCAN_AddMessageToTxFifoQ(
            (FDCAN_HandleTypeDef *)port->config.can_bus.handle,
            &header,
            (uint8_t *)frame->data));
    }
#endif

    return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
}

static bool cluster_stm32_can_receive(void *context, cluster_can_frame_t *frame) {
    cluster_stm32_hal_port_t *port;

    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.can_receive != NULL) {
        return port->config.can_receive(port->config.user_context, frame);
    }

#if defined(HAL_CAN_MODULE_ENABLED)
    if (port->config.can_bus.backend == CLUSTER_STM32_CAN_BACKEND_BXCAN &&
        port->config.can_bus.handle != NULL) {
        CAN_RxHeaderTypeDef header;

        if (HAL_CAN_GetRxFifoFillLevel((CAN_HandleTypeDef *)port->config.can_bus.handle,
                                       CAN_RX_FIFO0) == 0U) {
            return false;
        }

        if (HAL_CAN_GetRxMessage((CAN_HandleTypeDef *)port->config.can_bus.handle,
                                 CAN_RX_FIFO0,
                                 &header,
                                 frame->data) != 0) {
            return false;
        }

        frame->id = header.StdId;
        frame->dlc = (uint8_t)header.DLC;
        return true;
    }
#endif

#if defined(HAL_FDCAN_MODULE_ENABLED)
    if (port->config.can_bus.backend == CLUSTER_STM32_CAN_BACKEND_FDCAN &&
        port->config.can_bus.handle != NULL) {
        FDCAN_RxHeaderTypeDef header;

        if (HAL_FDCAN_GetRxFifoFillLevel(
                (FDCAN_HandleTypeDef *)port->config.can_bus.handle,
                port->config.can_bus.rx_fifo_index) == 0U) {
            return false;
        }

        if (HAL_FDCAN_GetRxMessage((FDCAN_HandleTypeDef *)port->config.can_bus.handle,
                                   port->config.can_bus.rx_fifo_index,
                                   &header,
                                   frame->data) != 0) {
            return false;
        }

        frame->id = header.Identifier;
        frame->dlc = (uint8_t)(header.DataLength >> 16U);
        return true;
    }
#endif

    return false;
}

static cluster_platform_status_t cluster_stm32_write_gpio(void *port_handle,
                                                          const cluster_stm32_gpio_t *gpio,
                                                          bool enabled) {
#if defined(HAL_GPIO_MODULE_ENABLED)
    GPIO_PinState state;

    if (gpio == NULL || gpio->port == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    state = enabled == gpio->active_high ? GPIO_PIN_SET : GPIO_PIN_RESET;
    HAL_GPIO_WritePin((GPIO_TypeDef *)gpio->port, gpio->pin, state);
    return CLUSTER_PLATFORM_STATUS_OK;
#else
    (void)port_handle;
    (void)gpio;
    (void)enabled;
    return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
#endif
}

static bool cluster_stm32_read_gpio(const cluster_stm32_gpio_t *gpio) {
#if defined(HAL_GPIO_MODULE_ENABLED)
    GPIO_PinState state;

    if (gpio == NULL || gpio->port == NULL) {
        return false;
    }

    state = HAL_GPIO_ReadPin((GPIO_TypeDef *)gpio->port, gpio->pin);
    return (state == GPIO_PIN_SET) == gpio->active_high;
#else
    (void)gpio;
    return false;
#endif
}

static cluster_platform_status_t cluster_stm32_set_precharge_drive(void *context,
                                                                   bool enabled) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    return cluster_stm32_write_gpio(port, &port->config.precharge_drive, enabled);
}

static cluster_platform_status_t cluster_stm32_set_main_contactor_drive(
    void *context,
    bool enabled) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    return cluster_stm32_write_gpio(port, &port->config.main_contactor_drive, enabled);
}

static bool cluster_stm32_read_precharge_feedback(void *context) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    return cluster_stm32_read_gpio(&port->config.precharge_feedback);
}

static bool cluster_stm32_read_main_contactor_feedback(void *context) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    return cluster_stm32_read_gpio(&port->config.main_contactor_feedback);
}

static cluster_platform_status_t cluster_stm32_sample_measurements(
    void *context,
    cluster_node_measurements_t *measurements) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.sample_measurements == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return port->config.sample_measurements(port->config.user_context, measurements);
}

static cluster_platform_status_t cluster_stm32_watchdog_kick(void *context) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.watchdog_kick != NULL) {
        return port->config.watchdog_kick(port->config.user_context);
    }

#if defined(HAL_IWDG_MODULE_ENABLED)
    if (port->config.watchdog_handle != NULL) {
        return cluster_stm32_map_hal_status(
            HAL_IWDG_Refresh((IWDG_HandleTypeDef *)port->config.watchdog_handle));
    }
#endif

    return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
}

static cluster_platform_status_t cluster_stm32_flash_read(void *context,
                                                          uint32_t address,
                                                          void *buffer,
                                                          uint32_t length) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.flash_read == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return port->config.flash_read(port->config.user_context, address, buffer, length);
}

static cluster_platform_status_t cluster_stm32_flash_write(void *context,
                                                           uint32_t address,
                                                           const void *data,
                                                           uint32_t length) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.flash_write == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return port->config.flash_write(port->config.user_context, address, data, length);
}

static cluster_platform_status_t cluster_stm32_flash_erase(void *context,
                                                           uint32_t address,
                                                           uint32_t length) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.flash_erase == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return port->config.flash_erase(port->config.user_context, address, length);
}

static cluster_platform_status_t cluster_stm32_schedule_boot_slot(void *context,
                                                                  uint8_t slot_id) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.schedule_boot_slot == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return port->config.schedule_boot_slot(port->config.user_context, slot_id);
}

static void cluster_stm32_request_reset(void *context) {
    cluster_stm32_hal_port_t *port;
    port = (cluster_stm32_hal_port_t *)context;
    if (port->config.request_reset != NULL) {
        port->config.request_reset(port->config.user_context);
        return;
    }

#ifdef CLUSTER_STM32_HAL_HEADER
    HAL_NVIC_SystemReset();
#endif
}

void cluster_stm32_hal_port_init(cluster_stm32_hal_port_t *port,
                                 const cluster_stm32_hal_config_t *config) {
    if (port == NULL || config == NULL) {
        return;
    }

    memset(port, 0, sizeof(*port));
    port->config = *config;
    port->api.now_ms = cluster_stm32_now_ms;
    port->api.can_send = cluster_stm32_can_send;
    port->api.can_receive = cluster_stm32_can_receive;
    port->api.set_precharge_drive = cluster_stm32_set_precharge_drive;
    port->api.set_main_contactor_drive = cluster_stm32_set_main_contactor_drive;
    port->api.read_precharge_feedback = cluster_stm32_read_precharge_feedback;
    port->api.read_main_contactor_feedback =
        cluster_stm32_read_main_contactor_feedback;
    port->api.sample_measurements = cluster_stm32_sample_measurements;
    port->api.watchdog_kick = cluster_stm32_watchdog_kick;
    port->api.flash_read = cluster_stm32_flash_read;
    port->api.flash_write = cluster_stm32_flash_write;
    port->api.flash_erase = cluster_stm32_flash_erase;
    port->api.schedule_boot_slot = cluster_stm32_schedule_boot_slot;
    port->api.request_reset = cluster_stm32_request_reset;
}

void cluster_stm32_hal_bind_platform(cluster_stm32_hal_port_t *port,
                                     cluster_platform_t *platform) {
    if (port == NULL || platform == NULL) {
        return;
    }

    cluster_platform_init(platform, &port->api, port);
}
