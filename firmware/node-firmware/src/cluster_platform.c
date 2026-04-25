#include "cluster_platform.h"
#include <stddef.h>

void cluster_platform_init(cluster_platform_t *platform,
                           const cluster_platform_api_t *api,
                           void *context) {
    platform->api = api;
    platform->context = context;
}

uint32_t cluster_platform_now_ms(const cluster_platform_t *platform) {
    if (platform == NULL || platform->api == NULL || platform->api->now_ms == NULL) {
        return 0U;
    }

    return platform->api->now_ms(platform->context);
}

cluster_platform_status_t cluster_platform_can_send(
    const cluster_platform_t *platform,
    const cluster_can_frame_t *frame) {
    if (platform == NULL || platform->api == NULL || platform->api->can_send == NULL ||
        frame == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return platform->api->can_send(platform->context, frame);
}

bool cluster_platform_can_receive(const cluster_platform_t *platform,
                                  cluster_can_frame_t *frame) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->can_receive == NULL || frame == NULL) {
        return false;
    }

    return platform->api->can_receive(platform->context, frame);
}

cluster_platform_status_t cluster_platform_set_precharge_drive(
    const cluster_platform_t *platform,
    bool enabled) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->set_precharge_drive == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return platform->api->set_precharge_drive(platform->context, enabled);
}

cluster_platform_status_t cluster_platform_set_main_contactor_drive(
    const cluster_platform_t *platform,
    bool enabled) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->set_main_contactor_drive == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return platform->api->set_main_contactor_drive(platform->context, enabled);
}

bool cluster_platform_read_precharge_feedback(const cluster_platform_t *platform) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->read_precharge_feedback == NULL) {
        return false;
    }

    return platform->api->read_precharge_feedback(platform->context);
}

bool cluster_platform_read_main_contactor_feedback(
    const cluster_platform_t *platform) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->read_main_contactor_feedback == NULL) {
        return false;
    }

    return platform->api->read_main_contactor_feedback(platform->context);
}

cluster_platform_status_t cluster_platform_sample_measurements(
    const cluster_platform_t *platform,
    cluster_node_measurements_t *measurements) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->sample_measurements == NULL || measurements == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return platform->api->sample_measurements(platform->context, measurements);
}

cluster_platform_status_t cluster_platform_watchdog_kick(
    const cluster_platform_t *platform) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->watchdog_kick == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return platform->api->watchdog_kick(platform->context);
}

cluster_platform_status_t cluster_platform_flash_read(
    const cluster_platform_t *platform,
    uint32_t address,
    void *buffer,
    uint32_t length) {
    if (platform == NULL || platform->api == NULL || platform->api->flash_read == NULL ||
        buffer == NULL || length == 0U) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return platform->api->flash_read(platform->context, address, buffer, length);
}

cluster_platform_status_t cluster_platform_flash_write(
    const cluster_platform_t *platform,
    uint32_t address,
    const void *data,
    uint32_t length) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->flash_write == NULL || data == NULL || length == 0U) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return platform->api->flash_write(platform->context, address, data, length);
}

cluster_platform_status_t cluster_platform_flash_erase(
    const cluster_platform_t *platform,
    uint32_t address,
    uint32_t length) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->flash_erase == NULL || length == 0U) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }

    return platform->api->flash_erase(platform->context, address, length);
}

cluster_platform_status_t cluster_platform_schedule_boot_slot(
    const cluster_platform_t *platform,
    uint8_t slot_id) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->schedule_boot_slot == NULL) {
        return CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
    }

    return platform->api->schedule_boot_slot(platform->context, slot_id);
}

void cluster_platform_request_reset(const cluster_platform_t *platform) {
    if (platform == NULL || platform->api == NULL ||
        platform->api->request_reset == NULL) {
        return;
    }

    platform->api->request_reset(platform->context);
}
