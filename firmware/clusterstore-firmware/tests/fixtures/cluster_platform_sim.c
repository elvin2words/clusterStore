#include "cluster_platform_sim.h"
#include <stdlib.h>
#include <string.h>

static bool range_valid(const cluster_platform_sim_t *sim,
                        uint32_t address,
                        uint32_t length) {
    return sim != NULL && sim->bytes != NULL && length > 0U &&
           address >= sim->base_address &&
           length <= sim->size_bytes &&
           (address - sim->base_address) <= (sim->size_bytes - length);
}

static cluster_platform_status_t sim_flash_read(void *ctx,
                                                uint32_t address,
                                                void *buf,
                                                uint32_t len) {
    cluster_platform_sim_t *s = (cluster_platform_sim_t *)ctx;
    if (!range_valid(s, address, len) || buf == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }
    memcpy(buf, s->bytes + (address - s->base_address), len);
    return CLUSTER_PLATFORM_STATUS_OK;
}

static cluster_platform_status_t sim_flash_write(void *ctx,
                                                 uint32_t address,
                                                 const void *data,
                                                 uint32_t len) {
    cluster_platform_sim_t *s = (cluster_platform_sim_t *)ctx;
    const uint8_t *src;
    uint8_t *dst;
    uint32_t i;

    if (!range_valid(s, address, len) || data == NULL) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }
    src = (const uint8_t *)data;
    dst = s->bytes + (address - s->base_address);
    for (i = 0U; i < len; i++) {
        if (dst[i] != 0xFFU) {
            return CLUSTER_PLATFORM_STATUS_ERROR;
        }
    }
    memcpy(dst, src, len);
    return CLUSTER_PLATFORM_STATUS_OK;
}

static cluster_platform_status_t sim_flash_erase(void *ctx,
                                                 uint32_t address,
                                                 uint32_t len) {
    cluster_platform_sim_t *s = (cluster_platform_sim_t *)ctx;
    uint32_t offset;

    if (!range_valid(s, address, len) || s->page_size_bytes == 0U ||
        ((address - s->base_address) % s->page_size_bytes) != 0U ||
        (len % s->page_size_bytes) != 0U) {
        return CLUSTER_PLATFORM_STATUS_INVALID_ARGUMENT;
    }
    offset = address - s->base_address;
    memset(s->bytes + offset, 0xFF, len);
    return CLUSTER_PLATFORM_STATUS_OK;
}

static const cluster_platform_api_t sim_api = {
    .flash_read = sim_flash_read,
    .flash_write = sim_flash_write,
    .flash_erase = sim_flash_erase
};

bool cluster_platform_sim_init(cluster_platform_sim_t *sim,
                               uint32_t base_address,
                               uint32_t size_bytes,
                               uint32_t page_size_bytes) {
    if (sim == NULL || size_bytes == 0U || page_size_bytes == 0U ||
        (size_bytes % page_size_bytes) != 0U) {
        return false;
    }
    memset(sim, 0, sizeof(*sim));
    sim->bytes = (uint8_t *)malloc(size_bytes);
    if (sim->bytes == NULL) {
        return false;
    }
    memset(sim->bytes, 0xFF, size_bytes);
    sim->base_address = base_address;
    sim->size_bytes = size_bytes;
    sim->page_size_bytes = page_size_bytes;
    return true;
}

void cluster_platform_sim_deinit(cluster_platform_sim_t *sim) {
    if (sim == NULL) {
        return;
    }
    free(sim->bytes);
    sim->bytes = NULL;
}

void cluster_platform_sim_bind(cluster_platform_sim_t *sim,
                               cluster_platform_t *platform) {
    if (sim == NULL || platform == NULL) {
        return;
    }
    cluster_platform_init(platform, &sim_api, sim);
}

uint8_t *cluster_platform_sim_ptr(cluster_platform_sim_t *sim, uint32_t address) {
    if (!range_valid(sim, address, 1U)) {
        return NULL;
    }
    return sim->bytes + (address - sim->base_address);
}
