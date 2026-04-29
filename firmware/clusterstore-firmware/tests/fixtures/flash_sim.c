#include "flash_sim.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bool cs_flash_sim_range_is_valid(const cs_flash_sim_t *sim,
                                        uint32_t address,
                                        uint32_t length) {
    return sim != NULL && sim->bytes != NULL && address >= sim->base_address &&
           length <= sim->size_bytes &&
           (address - sim->base_address) <= (sim->size_bytes - length);
}

static cs_status_t cs_flash_sim_read(void *context,
                                     uint32_t address,
                                     void *buffer,
                                     uint32_t length) {
    cs_flash_sim_t *sim;

    sim = (cs_flash_sim_t *)context;
    if (!cs_flash_sim_range_is_valid(sim, address, length) || buffer == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memcpy(buffer, sim->bytes + (address - sim->base_address), length);
    return CS_STATUS_OK;
}

static cs_status_t cs_flash_sim_write(void *context,
                                      uint32_t address,
                                      const void *data,
                                      uint32_t length) {
    const uint8_t *source;
    uint8_t *destination;
    uint32_t index;
    cs_flash_sim_t *sim;

    sim = (cs_flash_sim_t *)context;
    if (!cs_flash_sim_range_is_valid(sim, address, length) || data == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    source = (const uint8_t *)data;
    destination = sim->bytes + (address - sim->base_address);
    for (index = 0U; index < length; index += 1U) {
        if (destination[index] != 0xFFU) {
            return CS_STATUS_ERROR;
        }
    }

    memcpy(destination, source, length);
    return CS_STATUS_OK;
}

static cs_status_t cs_flash_sim_erase(void *context,
                                      uint32_t address,
                                      uint32_t length) {
    cs_flash_sim_t *sim;

    sim = (cs_flash_sim_t *)context;
    if (!cs_flash_sim_range_is_valid(sim, address, length) || length == 0U ||
        sim->page_size_bytes == 0U ||
        ((address - sim->base_address) % sim->page_size_bytes) != 0U ||
        (length % sim->page_size_bytes) != 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(sim->bytes + (address - sim->base_address), 0xFF, length);
    return CS_STATUS_OK;
}

bool cs_flash_sim_init(cs_flash_sim_t *sim,
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

void cs_flash_sim_deinit(cs_flash_sim_t *sim) {
    if (sim == NULL) {
        return;
    }

    free(sim->bytes);
    sim->bytes = NULL;
    sim->base_address = 0U;
    sim->size_bytes = 0U;
    sim->page_size_bytes = 0U;
}

void cs_flash_sim_bind_platform(cs_flash_sim_t *sim, cs_platform_t *platform) {
    if (sim == NULL || platform == NULL) {
        return;
    }

    cs_platform_init(platform);
    platform->flash.context = sim;
    platform->flash.page_size_bytes = sim->page_size_bytes;
    platform->flash.read = cs_flash_sim_read;
    platform->flash.write = cs_flash_sim_write;
    platform->flash.erase = cs_flash_sim_erase;
}

uint8_t *cs_flash_sim_address_ptr(cs_flash_sim_t *sim, uint32_t address) {
    if (!cs_flash_sim_range_is_valid(sim, address, 1U)) {
        return NULL;
    }

    return sim->bytes + (address - sim->base_address);
}
