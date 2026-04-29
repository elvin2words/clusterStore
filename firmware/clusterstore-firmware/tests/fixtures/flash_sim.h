#ifndef CS_FLASH_SIM_H
#define CS_FLASH_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "cs_cluster_platform.h"

typedef struct {
    uint8_t *bytes;
    uint32_t base_address;
    uint32_t size_bytes;
    uint32_t page_size_bytes;
} cs_flash_sim_t;

bool cs_flash_sim_init(cs_flash_sim_t *sim,
                       uint32_t base_address,
                       uint32_t size_bytes,
                       uint32_t page_size_bytes);
void cs_flash_sim_deinit(cs_flash_sim_t *sim);
void cs_flash_sim_bind_platform(cs_flash_sim_t *sim, cs_platform_t *platform);
uint8_t *cs_flash_sim_address_ptr(cs_flash_sim_t *sim, uint32_t address);

#endif
