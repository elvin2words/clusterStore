#ifndef CLUSTER_PLATFORM_H
#define CLUSTER_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_node_controller.h"

#define CLUSTER_CAN_MAX_DLC 8U

typedef enum {
    CLUSTER_PLATFORM_STATUS_OK = 0,
    CLUSTER_PLATFORM_STATUS_ERROR = 1,
    CLUSTER_PLATFORM_STATUS_TIMEOUT = 2,
    CLUSTER_PLATFORM_STATUS_UNSUPPORTED = 3,
    CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT = 4
} cluster_platform_status_t;

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[CLUSTER_CAN_MAX_DLC];
} cluster_can_frame_t;

typedef struct cluster_platform_api cluster_platform_api_t;

struct cluster_platform_api {
    uint32_t (*now_ms)(void *context);
    cluster_platform_status_t (*can_send)(void *context,
                                          const cluster_can_frame_t *frame);
    bool (*can_receive)(void *context, cluster_can_frame_t *frame);
    cluster_platform_status_t (*set_precharge_drive)(void *context, bool enabled);
    cluster_platform_status_t (*set_main_contactor_drive)(void *context,
                                                          bool enabled);
    bool (*read_precharge_feedback)(void *context);
    bool (*read_main_contactor_feedback)(void *context);
    cluster_platform_status_t (*sample_measurements)(
        void *context,
        cluster_node_measurements_t *measurements);
    cluster_platform_status_t (*watchdog_kick)(void *context);
    cluster_platform_status_t (*flash_read)(void *context,
                                            uint32_t address,
                                            void *buffer,
                                            uint32_t length);
    cluster_platform_status_t (*flash_write)(void *context,
                                             uint32_t address,
                                             const void *data,
                                             uint32_t length);
    cluster_platform_status_t (*flash_erase)(void *context,
                                             uint32_t address,
                                             uint32_t length);
    cluster_platform_status_t (*schedule_boot_slot)(void *context, uint8_t slot_id);
    void (*request_reset)(void *context);
};

typedef struct {
    const cluster_platform_api_t *api;
    void *context;
} cluster_platform_t;

void cluster_platform_init(cluster_platform_t *platform,
                           const cluster_platform_api_t *api,
                           void *context);
uint32_t cluster_platform_now_ms(const cluster_platform_t *platform);
cluster_platform_status_t cluster_platform_can_send(
    const cluster_platform_t *platform,
    const cluster_can_frame_t *frame);
bool cluster_platform_can_receive(const cluster_platform_t *platform,
                                  cluster_can_frame_t *frame);
cluster_platform_status_t cluster_platform_set_precharge_drive(
    const cluster_platform_t *platform,
    bool enabled);
cluster_platform_status_t cluster_platform_set_main_contactor_drive(
    const cluster_platform_t *platform,
    bool enabled);
bool cluster_platform_read_precharge_feedback(const cluster_platform_t *platform);
bool cluster_platform_read_main_contactor_feedback(
    const cluster_platform_t *platform);
cluster_platform_status_t cluster_platform_sample_measurements(
    const cluster_platform_t *platform,
    cluster_node_measurements_t *measurements);
cluster_platform_status_t cluster_platform_watchdog_kick(
    const cluster_platform_t *platform);
cluster_platform_status_t cluster_platform_flash_read(
    const cluster_platform_t *platform,
    uint32_t address,
    void *buffer,
    uint32_t length);
cluster_platform_status_t cluster_platform_flash_write(
    const cluster_platform_t *platform,
    uint32_t address,
    const void *data,
    uint32_t length);
cluster_platform_status_t cluster_platform_flash_erase(
    const cluster_platform_t *platform,
    uint32_t address,
    uint32_t length);
cluster_platform_status_t cluster_platform_schedule_boot_slot(
    const cluster_platform_t *platform,
    uint8_t slot_id);
void cluster_platform_request_reset(const cluster_platform_t *platform);

#endif
