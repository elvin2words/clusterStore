#ifndef CLUSTER_STM32_HAL_H
#define CLUSTER_STM32_HAL_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_platform.h"

typedef enum {
    CLUSTER_STM32_CAN_BACKEND_UNSPECIFIED = 0,
    CLUSTER_STM32_CAN_BACKEND_BXCAN = 1,
    CLUSTER_STM32_CAN_BACKEND_FDCAN = 2
} cluster_stm32_can_backend_t;

typedef struct {
    void *port;
    uint16_t pin;
    bool active_high;
} cluster_stm32_gpio_t;

typedef struct {
    void *handle;
    cluster_stm32_can_backend_t backend;
    uint32_t tx_identifier_type;
    uint32_t rx_fifo_index;
} cluster_stm32_can_bus_t;

typedef uint32_t (*cluster_stm32_now_ms_fn)(void *context);
typedef cluster_platform_status_t (*cluster_stm32_can_send_fn)(
    void *context,
    const cluster_can_frame_t *frame);
typedef bool (*cluster_stm32_can_receive_fn)(void *context,
                                             cluster_can_frame_t *frame);
typedef cluster_platform_status_t (*cluster_stm32_sample_measurements_fn)(
    void *context,
    cluster_node_measurements_t *measurements);
typedef cluster_platform_status_t (*cluster_stm32_flash_read_fn)(void *context,
                                                                 uint32_t address,
                                                                 void *buffer,
                                                                 uint32_t length);
typedef cluster_platform_status_t (*cluster_stm32_flash_write_fn)(
    void *context,
    uint32_t address,
    const void *data,
    uint32_t length);
typedef cluster_platform_status_t (*cluster_stm32_flash_erase_fn)(void *context,
                                                                  uint32_t address,
                                                                  uint32_t length);
typedef cluster_platform_status_t (*cluster_stm32_schedule_boot_slot_fn)(
    void *context,
    uint8_t slot_id);
typedef cluster_platform_status_t (*cluster_stm32_watchdog_kick_fn)(void *context);
typedef void (*cluster_stm32_request_reset_fn)(void *context);

typedef struct {
    cluster_stm32_can_bus_t can_bus;
    cluster_stm32_gpio_t precharge_drive;
    cluster_stm32_gpio_t main_contactor_drive;
    cluster_stm32_gpio_t precharge_feedback;
    cluster_stm32_gpio_t main_contactor_feedback;
    void *watchdog_handle;
    void *user_context;
    cluster_stm32_now_ms_fn now_ms;
    cluster_stm32_can_send_fn can_send;
    cluster_stm32_can_receive_fn can_receive;
    cluster_stm32_sample_measurements_fn sample_measurements;
    cluster_stm32_flash_read_fn flash_read;
    cluster_stm32_flash_write_fn flash_write;
    cluster_stm32_flash_erase_fn flash_erase;
    cluster_stm32_schedule_boot_slot_fn schedule_boot_slot;
    cluster_stm32_watchdog_kick_fn watchdog_kick;
    cluster_stm32_request_reset_fn request_reset;
} cluster_stm32_hal_config_t;

typedef struct {
    cluster_stm32_hal_config_t config;
    cluster_platform_api_t api;
} cluster_stm32_hal_port_t;

void cluster_stm32_hal_port_init(cluster_stm32_hal_port_t *port,
                                 const cluster_stm32_hal_config_t *config);
void cluster_stm32_hal_bind_platform(cluster_stm32_hal_port_t *port,
                                     cluster_platform_t *platform);

#endif
