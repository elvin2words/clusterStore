#include "cs_cluster_platform.h"
#include <stddef.h>

void cs_platform_init(cs_platform_t *platform) {
    if (platform == NULL) {
        return;
    }

    platform->flash.context = NULL;
    platform->flash.page_size_bytes = 0U;
    platform->flash.read = NULL;
    platform->flash.write = NULL;
    platform->flash.erase = NULL;
    platform->can.context = NULL;
    platform->can.send = NULL;
    platform->can.receive = NULL;
    platform->time.context = NULL;
    platform->time.monotonic_ms = NULL;
    platform->log.context = NULL;
    platform->log.write = NULL;
}

cs_status_t cs_platform_flash_read(const cs_platform_t *platform,
                                   uint32_t address,
                                   void *buffer,
                                   uint32_t length) {
    if (platform == NULL || platform->flash.read == NULL || buffer == NULL ||
        length == 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return platform->flash.read(platform->flash.context, address, buffer, length);
}

cs_status_t cs_platform_flash_write(const cs_platform_t *platform,
                                    uint32_t address,
                                    const void *data,
                                    uint32_t length) {
    if (platform == NULL || platform->flash.write == NULL || data == NULL ||
        length == 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return platform->flash.write(platform->flash.context, address, data, length);
}

cs_status_t cs_platform_flash_erase(const cs_platform_t *platform,
                                    uint32_t address,
                                    uint32_t length) {
    if (platform == NULL || platform->flash.erase == NULL || length == 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return platform->flash.erase(platform->flash.context, address, length);
}

cs_status_t cs_platform_can_send(const cs_platform_t *platform,
                                 const cs_can_frame_t *frame) {
    if (platform == NULL || platform->can.send == NULL || frame == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    return platform->can.send(platform->can.context, frame);
}

bool cs_platform_can_receive(const cs_platform_t *platform,
                             cs_can_frame_t *frame) {
    if (platform == NULL || platform->can.receive == NULL || frame == NULL) {
        return false;
    }

    return platform->can.receive(platform->can.context, frame);
}

uint32_t cs_platform_monotonic_ms(const cs_platform_t *platform) {
    if (platform == NULL || platform->time.monotonic_ms == NULL) {
        return 0U;
    }

    return platform->time.monotonic_ms(platform->time.context);
}

void cs_platform_log(const cs_platform_t *platform,
                     cs_log_level_t level,
                     const char *message) {
    if (platform == NULL || platform->log.write == NULL || message == NULL) {
        return;
    }

    platform->log.write(platform->log.context, level, message);
}
