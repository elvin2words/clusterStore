#ifndef CS_CLUSTER_BRIDGE_G474_H
#define CS_CLUSTER_BRIDGE_G474_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_platform.h"
#include "cluster_stm32_hal.h"
#include "cs_bsp_g474.h"

typedef struct {
    uint8_t default_soc_pct;
    uint8_t local_fault_flags;
    bool balancing_supported;
    bool maintenance_lockout;
    bool service_lockout;
} cs_cluster_bridge_g474_config_t;

typedef struct {
    cs_bsp_g474_t *bsp;
    cs_cluster_bridge_g474_config_t config;
    cluster_stm32_hal_port_t hal_port;
    cluster_platform_t cluster_platform;
    uint8_t scheduled_boot_slot;
    bool reset_requested;
} cs_cluster_bridge_g474_t;

cs_status_t cs_cluster_bridge_g474_init(cs_cluster_bridge_g474_t *bridge,
                                        cs_bsp_g474_t *bsp,
                                        const cs_cluster_bridge_g474_config_t *config);
void cs_cluster_bridge_g474_bind_platform(cs_cluster_bridge_g474_t *bridge,
                                          cluster_platform_t *platform);
uint8_t cs_cluster_bridge_g474_scheduled_boot_slot(
    const cs_cluster_bridge_g474_t *bridge);
bool cs_cluster_bridge_g474_reset_requested(const cs_cluster_bridge_g474_t *bridge);

#endif
