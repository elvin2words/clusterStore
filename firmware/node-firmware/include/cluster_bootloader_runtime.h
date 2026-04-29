#ifndef CLUSTER_BOOTLOADER_RUNTIME_H
#define CLUSTER_BOOTLOADER_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_boot_control.h"
#include "cluster_flash_layout.h"
#include "cluster_persistent_state.h"
#include "cluster_platform.h"

typedef bool (*cluster_bootloader_verify_image_fn)(void *context,
                                                   uint32_t image_address,
                                                   uint32_t image_size_bytes,
                                                   uint32_t expected_crc32);
typedef void (*cluster_bootloader_jump_fn)(void *context,
                                           uint32_t vector_table_address);

typedef struct {
    const cluster_platform_t *platform;
    cluster_flash_layout_config_t flash_layout;
    uint8_t default_active_slot_id;
    const char *default_version;
    bool verify_crc32_before_boot;
    cluster_bootloader_verify_image_fn verify_image;
    void *verify_context;
    cluster_bootloader_jump_fn jump_to_image;
    void *jump_context;
} cluster_bootloader_runtime_config_t;

typedef struct {
    cluster_bootloader_runtime_config_t config;
    cluster_flash_layout_t flash_layout;
    cluster_persistent_state_t persistent_state;
} cluster_bootloader_runtime_t;

bool cluster_bootloader_runtime_init(
    cluster_bootloader_runtime_t *runtime,
    const cluster_bootloader_runtime_config_t *config);
cluster_boot_action_t cluster_bootloader_runtime_select(
    cluster_bootloader_runtime_t *runtime,
    uint8_t *slot_id);
cluster_boot_action_t cluster_bootloader_runtime_boot(
    cluster_bootloader_runtime_t *runtime,
    uint8_t *slot_id);
bool cluster_bootloader_runtime_verify_slot_crc32(
    const cluster_platform_t *platform,
    uint32_t image_address,
    uint32_t image_size_bytes,
    uint32_t expected_crc32);

#endif
