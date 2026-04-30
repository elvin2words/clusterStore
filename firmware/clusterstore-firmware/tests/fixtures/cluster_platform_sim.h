#ifndef CLUSTER_PLATFORM_SIM_H
#define CLUSTER_PLATFORM_SIM_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_platform.h"

typedef struct {
    uint8_t *bytes;
    uint32_t base_address;
    uint32_t size_bytes;
    uint32_t page_size_bytes;
} cluster_platform_sim_t;

bool cluster_platform_sim_init(cluster_platform_sim_t *sim,
                               uint32_t base_address,
                               uint32_t size_bytes,
                               uint32_t page_size_bytes);
void cluster_platform_sim_deinit(cluster_platform_sim_t *sim);
void cluster_platform_sim_bind(cluster_platform_sim_t *sim,
                               cluster_platform_t *platform);
uint8_t *cluster_platform_sim_ptr(cluster_platform_sim_t *sim, uint32_t address);

#endif
