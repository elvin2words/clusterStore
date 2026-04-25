#include "cluster_ota_manager.h"
#include <stddef.h>

static void cluster_copy_version(char *destination, const char *source) {
    uint8_t index;
    if (destination == NULL) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    for (index = 0U; index < (CLUSTER_OTA_VERSION_TEXT_BYTES - 1U) &&
                      source[index] != '\0';
         index += 1U) {
        destination[index] = source[index];
    }
    destination[index] = '\0';
}

static void cluster_clear_slot(cluster_ota_slot_t *slot) {
    if (slot == NULL) {
        return;
    }

    slot->slot_id = 0U;
    slot->remaining_boot_attempts = 0U;
    slot->rollback_requested = false;
    slot->state = CLUSTER_OTA_SLOT_EMPTY;
    slot->version[0] = '\0';
    slot->image_size_bytes = 0U;
    slot->image_crc32 = 0U;
}

void cluster_ota_manager_init(cluster_ota_manager_t *manager) {
    cluster_clear_slot(&manager->active_slot);
    cluster_clear_slot(&manager->candidate_slot);
    cluster_clear_slot(&manager->rollback_slot);
}

void cluster_ota_manager_set_confirmed_active(cluster_ota_manager_t *manager,
                                              uint8_t slot_id,
                                              const char *version,
                                              uint32_t image_size_bytes,
                                              uint32_t image_crc32) {
    manager->active_slot.slot_id = slot_id;
    manager->active_slot.remaining_boot_attempts = 0U;
    manager->active_slot.rollback_requested = false;
    manager->active_slot.state = CLUSTER_OTA_SLOT_CONFIRMED;
    cluster_copy_version(manager->active_slot.version, version);
    manager->active_slot.image_size_bytes = image_size_bytes;
    manager->active_slot.image_crc32 = image_crc32;
}

bool cluster_ota_manager_register_candidate(cluster_ota_manager_t *manager,
                                            uint8_t slot_id,
                                            const char *version,
                                            uint32_t image_size_bytes,
                                            uint32_t image_crc32) {
    if (version == NULL || version[0] == '\0' || image_size_bytes == 0U ||
        image_crc32 == 0U) {
        return false;
    }

    manager->candidate_slot.slot_id = slot_id;
    manager->candidate_slot.remaining_boot_attempts = 0U;
    manager->candidate_slot.rollback_requested = false;
    manager->candidate_slot.state = CLUSTER_OTA_SLOT_DOWNLOADED;
    cluster_copy_version(manager->candidate_slot.version, version);
    manager->candidate_slot.image_size_bytes = image_size_bytes;
    manager->candidate_slot.image_crc32 = image_crc32;
    return true;
}

bool cluster_ota_manager_activate_candidate(cluster_ota_manager_t *manager,
                                            uint8_t max_trial_boot_attempts) {
    if (manager->candidate_slot.state != CLUSTER_OTA_SLOT_DOWNLOADED ||
        max_trial_boot_attempts == 0U) {
        return false;
    }

    manager->rollback_slot = manager->active_slot;
    manager->active_slot = manager->candidate_slot;
    manager->active_slot.state = CLUSTER_OTA_SLOT_PENDING_TEST;
    manager->active_slot.remaining_boot_attempts = max_trial_boot_attempts;
    manager->active_slot.rollback_requested = false;
    cluster_clear_slot(&manager->candidate_slot);
    return true;
}

bool cluster_ota_manager_note_boot_attempt(cluster_ota_manager_t *manager) {
    if (manager->active_slot.state != CLUSTER_OTA_SLOT_PENDING_TEST) {
        return false;
    }

    if (manager->active_slot.remaining_boot_attempts > 0U) {
        manager->active_slot.remaining_boot_attempts -= 1U;
    }

    if (manager->active_slot.remaining_boot_attempts == 0U) {
        manager->active_slot.rollback_requested = true;
    }

    return true;
}

bool cluster_ota_manager_confirm_active(cluster_ota_manager_t *manager) {
    if (manager->active_slot.state != CLUSTER_OTA_SLOT_PENDING_TEST) {
        return false;
    }

    manager->active_slot.state = CLUSTER_OTA_SLOT_CONFIRMED;
    manager->active_slot.remaining_boot_attempts = 0U;
    manager->active_slot.rollback_requested = false;
    return true;
}

bool cluster_ota_manager_request_rollback(cluster_ota_manager_t *manager) {
    if (manager->rollback_slot.state != CLUSTER_OTA_SLOT_CONFIRMED) {
        manager->active_slot.rollback_requested = true;
        return false;
    }

    manager->candidate_slot = manager->active_slot;
    manager->candidate_slot.state = CLUSTER_OTA_SLOT_INVALID;
    manager->active_slot = manager->rollback_slot;
    manager->active_slot.rollback_requested = false;
    cluster_clear_slot(&manager->rollback_slot);
    return true;
}

bool cluster_ota_manager_should_rollback(const cluster_ota_manager_t *manager) {
    return manager->active_slot.rollback_requested;
}

const cluster_ota_slot_t *cluster_ota_manager_boot_slot(
    const cluster_ota_manager_t *manager) {
    return &manager->active_slot;
}

