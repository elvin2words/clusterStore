#ifndef CLUSTER_NODE_RUNTIME_H
#define CLUSTER_NODE_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_flash_layout.h"
#include "cluster_node_controller.h"
#include "cluster_persistent_state.h"
#include "cluster_platform.h"

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
} cluster_node_runtime_config_t;

typedef struct {
    cluster_platform_t platform;
    cluster_flash_layout_t flash_layout;
    cluster_persistent_state_t persistent_state;
    cluster_event_journal_t journal;
    cluster_event_record_t *journal_records;
    cluster_ota_manager_t ota_manager;
    cluster_node_controller_t controller;
    cluster_node_runtime_config_t config;
    uint32_t last_status_publish_ms;
    bool status_dirty;
} cluster_node_runtime_t;

bool cluster_node_runtime_init(cluster_node_runtime_t *runtime,
                               const cluster_platform_t *platform,
                               const cluster_node_runtime_config_t *config,
                               cluster_event_record_t *journal_records,
                               uint32_t now_ms);
bool cluster_node_runtime_step(cluster_node_runtime_t *runtime,
                               cluster_node_control_output_t *output);

bool cluster_node_runtime_register_ota_candidate(
    cluster_node_runtime_t *runtime,
    uint8_t slot_id,
    const char *version,
    uint32_t image_size_bytes,
    uint32_t image_crc32);
bool cluster_node_runtime_activate_ota_candidate(cluster_node_runtime_t *runtime);
bool cluster_node_runtime_confirm_ota(cluster_node_runtime_t *runtime);
bool cluster_node_runtime_request_ota_rollback(cluster_node_runtime_t *runtime);

#endif
