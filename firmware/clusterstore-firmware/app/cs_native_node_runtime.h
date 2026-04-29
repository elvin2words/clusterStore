#ifndef CS_NATIVE_NODE_RUNTIME_H
#define CS_NATIVE_NODE_RUNTIME_H

#include "cluster_node_runtime.h"
#include "cs_cluster_bridge_g474.h"

#define CS_NATIVE_NODE_RUNTIME_MAX_JOURNAL_RECORDS 1024U

typedef struct {
    uint8_t node_address;
    uint8_t default_active_slot_id;
    uint8_t max_trial_boot_attempts;
    uint16_t status_period_ms;
    uint16_t command_poll_budget;
    uint16_t journal_record_capacity;
    const char *default_firmware_version;
    cluster_flash_layout_config_t flash_layout;
    cluster_node_controller_config_t controller;
    cs_cluster_bridge_g474_config_t bridge;
} cs_native_node_runtime_config_t;

typedef struct {
    cs_bsp_g474_t *bsp;
    cs_cluster_bridge_g474_t bridge;
    cluster_platform_t cluster_platform;
    cluster_node_runtime_t runtime;
    cluster_event_record_t
        journal_records[CS_NATIVE_NODE_RUNTIME_MAX_JOURNAL_RECORDS];
    cluster_node_control_output_t last_output;
} cs_native_node_runtime_t;

void cs_native_node_runtime_config_init(cs_native_node_runtime_config_t *config);
cs_status_t cs_native_node_runtime_init(cs_native_node_runtime_t *runtime,
                                        cs_bsp_g474_t *bsp,
                                        const cs_native_node_runtime_config_t *config);
cs_status_t cs_native_node_runtime_step(cs_native_node_runtime_t *runtime);
const cluster_node_control_output_t *cs_native_node_runtime_last_output(
    const cs_native_node_runtime_t *runtime);

#endif
