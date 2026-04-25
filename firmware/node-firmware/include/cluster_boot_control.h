#ifndef CLUSTER_BOOT_CONTROL_H
#define CLUSTER_BOOT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_crc32.h"
#include "cluster_flash_layout.h"
#include "cluster_ota_manager.h"

#define CLUSTER_BOOT_CONTROL_MAGIC 0x4342544CUL
#define CLUSTER_BOOT_CONTROL_VERSION 1U
#define CLUSTER_BOOT_SLOT_COUNT 2U

typedef enum {
    CLUSTER_BOOT_ACTION_BOOT_ACTIVE = 0,
    CLUSTER_BOOT_ACTION_BOOT_FALLBACK = 1,
    CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER = 2
} cluster_boot_action_t;

typedef struct {
    uint8_t slot_id;
    uint8_t state;
    uint8_t rollback_requested;
    uint8_t remaining_boot_attempts;
    uint32_t image_address;
    uint32_t image_size_bytes;
    uint32_t image_crc32;
    char version[CLUSTER_OTA_VERSION_TEXT_BYTES];
} cluster_boot_slot_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t active_slot_id;
    uint8_t fallback_slot_id;
    uint8_t stay_in_bootloader;
    uint8_t reserved[3];
    cluster_boot_slot_record_t slots[CLUSTER_BOOT_SLOT_COUNT];
    uint32_t crc32;
} cluster_boot_control_block_t;

void cluster_boot_control_init(cluster_boot_control_block_t *control,
                               const cluster_flash_layout_t *layout,
                               uint8_t active_slot_id,
                               const char *active_version);
bool cluster_boot_control_validate(const cluster_boot_control_block_t *control);
void cluster_boot_control_update_crc(cluster_boot_control_block_t *control);
cluster_boot_action_t cluster_boot_control_select_boot_slot(
    cluster_boot_control_block_t *control,
    uint8_t *slot_id);
bool cluster_boot_control_activate_slot(cluster_boot_control_block_t *control,
                                        const cluster_flash_layout_t *layout,
                                        uint8_t slot_id,
                                        const char *version,
                                        uint32_t image_size_bytes,
                                        uint32_t image_crc32,
                                        uint8_t max_trial_boot_attempts);
bool cluster_boot_control_confirm_active(cluster_boot_control_block_t *control);
bool cluster_boot_control_request_rollback(cluster_boot_control_block_t *control);
void cluster_boot_control_from_ota_manager(cluster_boot_control_block_t *control,
                                           const cluster_flash_layout_t *layout,
                                           const cluster_ota_manager_t *manager);
void cluster_boot_control_to_ota_manager(const cluster_boot_control_block_t *control,
                                         cluster_ota_manager_t *manager);

#endif
