#include "cluster_bootloader_runtime.h"
#include "cluster_crc32.h"
#include <stddef.h>
#include <string.h>

#define CLUSTER_BOOTLOADER_VERIFY_CHUNK_BYTES 64U

static cluster_boot_slot_record_t *cluster_bootloader_find_slot(
    cluster_boot_control_block_t *control,
    uint8_t slot_id) {
    uint8_t index;

    if (control == NULL) {
        return NULL;
    }

    for (index = 0U; index < CLUSTER_BOOT_SLOT_COUNT; index += 1U) {
        if (control->slots[index].slot_id == slot_id) {
            return &control->slots[index];
        }
    }

    return NULL;
}

static bool cluster_bootloader_verify_slot(
    cluster_bootloader_runtime_t *runtime,
    const cluster_boot_slot_record_t *slot) {
    if (slot == NULL || slot->image_address == 0U || slot->image_size_bytes == 0U ||
        slot->image_crc32 == 0U) {
        return false;
    }

    if (runtime->config.verify_crc32_before_boot &&
        !cluster_bootloader_runtime_verify_slot_crc32(
            runtime->config.platform,
            slot->image_address,
            slot->image_size_bytes,
            slot->image_crc32)) {
        return false;
    }

    if (runtime->config.verify_image != NULL) {
        return runtime->config.verify_image(runtime->config.verify_context,
                                            slot->image_address,
                                            slot->image_size_bytes,
                                            slot->image_crc32);
    }

    return true;
}

static cluster_boot_action_t cluster_bootloader_runtime_next_action(
    cluster_bootloader_runtime_t *runtime,
    uint8_t *slot_id) {
    cluster_boot_action_t action;
    cluster_boot_control_block_t previous_control;

    previous_control = runtime->persistent_state.boot_control;
    action = cluster_boot_control_select_boot_slot(&runtime->persistent_state.boot_control,
                                                   slot_id);
    if (memcmp(&previous_control,
               &runtime->persistent_state.boot_control,
               sizeof(previous_control)) != 0 &&
        !cluster_persistent_state_save(&runtime->persistent_state)) {
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    return action;
}

bool cluster_bootloader_runtime_init(
    cluster_bootloader_runtime_t *runtime,
    const cluster_bootloader_runtime_config_t *config) {
    if (runtime == NULL || config == NULL || config->platform == NULL ||
        config->default_version == NULL ||
        !cluster_flash_layout_build(&config->flash_layout, &runtime->flash_layout)) {
        return false;
    }

    runtime->config = *config;
    cluster_persistent_state_init(&runtime->persistent_state,
                                  config->platform,
                                  &runtime->flash_layout,
                                  (uint16_t)(runtime->flash_layout.journal_size_bytes /
                                             sizeof(cluster_event_record_t)));
    return cluster_persistent_state_load(&runtime->persistent_state,
                                         config->default_active_slot_id,
                                         config->default_version);
}

cluster_boot_action_t cluster_bootloader_runtime_select(
    cluster_bootloader_runtime_t *runtime,
    uint8_t *slot_id) {
    uint8_t selected_slot_id = 0U;
    cluster_boot_slot_record_t *slot;
    uint8_t attempts = 0U;
    cluster_boot_action_t action;

    if (runtime == NULL || slot_id == NULL) {
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    while (attempts < (CLUSTER_BOOT_SLOT_COUNT + 1U)) {
        attempts += 1U;
        action = cluster_bootloader_runtime_next_action(runtime, &selected_slot_id);
        if (action == CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER) {
            return action;
        }

        slot = cluster_bootloader_find_slot(&runtime->persistent_state.boot_control,
                                            selected_slot_id);
        if (cluster_bootloader_verify_slot(runtime, slot)) {
            *slot_id = selected_slot_id;
            return action;
        }

        if (slot == NULL) {
            return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
        }

        slot->state = CLUSTER_OTA_SLOT_INVALID;
        slot->rollback_requested = 1U;
        cluster_boot_control_update_crc(&runtime->persistent_state.boot_control);
        if (!cluster_persistent_state_save(&runtime->persistent_state)) {
            return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
        }
    }

    return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
}

cluster_boot_action_t cluster_bootloader_runtime_boot(
    cluster_bootloader_runtime_t *runtime,
    uint8_t *slot_id) {
    cluster_boot_action_t action;
    cluster_boot_slot_record_t *slot;

    action = cluster_bootloader_runtime_select(runtime, slot_id);
    if (action == CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER) {
        return action;
    }

    slot = cluster_bootloader_find_slot(&runtime->persistent_state.boot_control, *slot_id);
    if (slot == NULL) {
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    if (runtime->config.jump_to_image != NULL) {
        runtime->config.jump_to_image(runtime->config.jump_context, slot->image_address);
    }

    return action;
}

bool cluster_bootloader_runtime_verify_slot_crc32(
    const cluster_platform_t *platform,
    uint32_t image_address,
    uint32_t image_size_bytes,
    uint32_t expected_crc32) {
    uint8_t buffer[CLUSTER_BOOTLOADER_VERIFY_CHUNK_BYTES];
    uint32_t crc = cluster_crc32_seed();
    uint32_t offset = 0U;
    uint32_t chunk_bytes;

    if (platform == NULL || image_address == 0U || image_size_bytes == 0U ||
        expected_crc32 == 0U) {
        return false;
    }

    while (offset < image_size_bytes) {
        chunk_bytes = image_size_bytes - offset;
        if (chunk_bytes > sizeof(buffer)) {
            chunk_bytes = sizeof(buffer);
        }

        if (cluster_platform_flash_read(platform,
                                        image_address + offset,
                                        buffer,
                                        chunk_bytes) !=
            CLUSTER_PLATFORM_STATUS_OK) {
            return false;
        }

        crc = cluster_crc32_update(crc, buffer, chunk_bytes);
        offset += chunk_bytes;
    }

    return cluster_crc32_finalize(crc) == expected_crc32;
}
