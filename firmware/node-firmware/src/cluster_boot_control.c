#include "cluster_boot_control.h"
#include <stddef.h>
#include <string.h>

static cluster_boot_slot_record_t *cluster_boot_find_slot(
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

static const cluster_boot_slot_record_t *cluster_boot_find_slot_const(
    const cluster_boot_control_block_t *control,
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

static void cluster_boot_copy_version(char *destination, const char *source) {
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

void cluster_boot_control_update_crc(cluster_boot_control_block_t *control) {
    uint32_t crc;

    if (control == NULL) {
        return;
    }

    control->crc32 = 0U;
    crc = cluster_crc32_compute(control,
                                (uint32_t)offsetof(cluster_boot_control_block_t, crc32));
    control->crc32 = crc;
}

void cluster_boot_control_init(cluster_boot_control_block_t *control,
                               const cluster_flash_layout_t *layout,
                               uint8_t active_slot_id,
                               const char *active_version) {
    cluster_boot_slot_record_t *slot_a;
    cluster_boot_slot_record_t *slot_b;

    (void)layout;

    if (control == NULL) {
        return;
    }

    memset(control, 0, sizeof(*control));
    control->magic = CLUSTER_BOOT_CONTROL_MAGIC;
    control->version = CLUSTER_BOOT_CONTROL_VERSION;
    control->active_slot_id = active_slot_id;
    control->fallback_slot_id =
        active_slot_id == CLUSTER_BOOT_SLOT_A ? CLUSTER_BOOT_SLOT_B : CLUSTER_BOOT_SLOT_A;
    control->stay_in_bootloader = 0U;

    slot_a = &control->slots[0];
    slot_b = &control->slots[1];

    slot_a->slot_id = CLUSTER_BOOT_SLOT_A;
    slot_b->slot_id = CLUSTER_BOOT_SLOT_B;
    slot_a->image_address = layout != NULL
                                ? cluster_flash_layout_slot_address(layout, CLUSTER_BOOT_SLOT_A)
                                : 0U;
    slot_b->image_address = layout != NULL
                                ? cluster_flash_layout_slot_address(layout, CLUSTER_BOOT_SLOT_B)
                                : 0U;

    if (active_slot_id == CLUSTER_BOOT_SLOT_A) {
        slot_a->state = CLUSTER_OTA_SLOT_CONFIRMED;
        cluster_boot_copy_version(slot_a->version, active_version);
    } else if (active_slot_id == CLUSTER_BOOT_SLOT_B) {
        slot_b->state = CLUSTER_OTA_SLOT_CONFIRMED;
        cluster_boot_copy_version(slot_b->version, active_version);
    }

    cluster_boot_control_update_crc(control);
}

bool cluster_boot_control_validate(const cluster_boot_control_block_t *control) {
    uint32_t expected_crc;

    if (control == NULL || control->magic != CLUSTER_BOOT_CONTROL_MAGIC ||
        control->version != CLUSTER_BOOT_CONTROL_VERSION) {
        return false;
    }

    expected_crc = cluster_crc32_compute(control,
                                         (uint32_t)offsetof(cluster_boot_control_block_t,
                                                            crc32));
    return expected_crc == control->crc32;
}

cluster_boot_action_t cluster_boot_control_select_boot_slot(
    cluster_boot_control_block_t *control,
    uint8_t *slot_id) {
    cluster_boot_slot_record_t *active_slot;
    cluster_boot_slot_record_t *fallback_slot;

    if (control == NULL || slot_id == NULL) {
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    active_slot = cluster_boot_find_slot(control, control->active_slot_id);
    fallback_slot = cluster_boot_find_slot(control, control->fallback_slot_id);

    if (control->stay_in_bootloader != 0U || active_slot == NULL) {
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    if (active_slot->rollback_requested != 0U) {
        if (fallback_slot != NULL && fallback_slot->state == CLUSTER_OTA_SLOT_CONFIRMED) {
            *slot_id = fallback_slot->slot_id;
            control->active_slot_id = fallback_slot->slot_id;
            control->fallback_slot_id = active_slot->slot_id;
            active_slot->state = CLUSTER_OTA_SLOT_INVALID;
            active_slot->rollback_requested = 0U;
            cluster_boot_control_update_crc(control);
            return CLUSTER_BOOT_ACTION_BOOT_FALLBACK;
        }

        control->stay_in_bootloader = 1U;
        cluster_boot_control_update_crc(control);
        return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
    }

    if (active_slot->state == CLUSTER_OTA_SLOT_PENDING_TEST) {
        if (active_slot->remaining_boot_attempts == 0U) {
            active_slot->rollback_requested = 1U;
            return cluster_boot_control_select_boot_slot(control, slot_id);
        }

        active_slot->remaining_boot_attempts -= 1U;
        *slot_id = active_slot->slot_id;
        cluster_boot_control_update_crc(control);
        return CLUSTER_BOOT_ACTION_BOOT_ACTIVE;
    }

    if (active_slot->state == CLUSTER_OTA_SLOT_CONFIRMED) {
        *slot_id = active_slot->slot_id;
        cluster_boot_control_update_crc(control);
        return CLUSTER_BOOT_ACTION_BOOT_ACTIVE;
    }

    if (fallback_slot != NULL && fallback_slot->state == CLUSTER_OTA_SLOT_CONFIRMED) {
        *slot_id = fallback_slot->slot_id;
        cluster_boot_control_update_crc(control);
        return CLUSTER_BOOT_ACTION_BOOT_FALLBACK;
    }

    control->stay_in_bootloader = 1U;
    cluster_boot_control_update_crc(control);
    return CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER;
}

bool cluster_boot_control_activate_slot(cluster_boot_control_block_t *control,
                                        const cluster_flash_layout_t *layout,
                                        uint8_t slot_id,
                                        const char *version,
                                        uint32_t image_size_bytes,
                                        uint32_t image_crc32,
                                        uint8_t max_trial_boot_attempts) {
    cluster_boot_slot_record_t *slot;

    if (control == NULL || layout == NULL || max_trial_boot_attempts == 0U) {
        return false;
    }

    slot = cluster_boot_find_slot(control, slot_id);
    if (slot == NULL) {
        return false;
    }

    slot->image_address = cluster_flash_layout_slot_address(layout, slot_id);
    slot->image_size_bytes = image_size_bytes;
    slot->image_crc32 = image_crc32;
    slot->state = CLUSTER_OTA_SLOT_PENDING_TEST;
    slot->rollback_requested = 0U;
    slot->remaining_boot_attempts = max_trial_boot_attempts;
    cluster_boot_copy_version(slot->version, version);

    control->fallback_slot_id = control->active_slot_id;
    control->active_slot_id = slot_id;
    control->stay_in_bootloader = 0U;
    cluster_boot_control_update_crc(control);
    return true;
}

bool cluster_boot_control_confirm_active(cluster_boot_control_block_t *control) {
    cluster_boot_slot_record_t *slot;

    if (control == NULL) {
        return false;
    }

    slot = cluster_boot_find_slot(control, control->active_slot_id);
    if (slot == NULL) {
        return false;
    }

    slot->state = CLUSTER_OTA_SLOT_CONFIRMED;
    slot->rollback_requested = 0U;
    slot->remaining_boot_attempts = 0U;
    cluster_boot_control_update_crc(control);
    return true;
}

bool cluster_boot_control_request_rollback(cluster_boot_control_block_t *control) {
    cluster_boot_slot_record_t *slot;

    if (control == NULL) {
        return false;
    }

    slot = cluster_boot_find_slot(control, control->active_slot_id);
    if (slot == NULL) {
        return false;
    }

    slot->rollback_requested = 1U;
    cluster_boot_control_update_crc(control);
    return true;
}

void cluster_boot_control_from_ota_manager(cluster_boot_control_block_t *control,
                                           const cluster_flash_layout_t *layout,
                                           const cluster_ota_manager_t *manager) {
    const cluster_ota_slot_t *slots[CLUSTER_BOOT_SLOT_COUNT];
    uint8_t index;
    cluster_boot_slot_record_t *record;

    if (control == NULL || layout == NULL || manager == NULL) {
        return;
    }

    slots[0] = &manager->active_slot;
    slots[1] = &manager->rollback_slot;

    for (index = 0U; index < CLUSTER_BOOT_SLOT_COUNT; index += 1U) {
        record = &control->slots[index];
        if (record->slot_id == 0U) {
            record->slot_id = (uint8_t)(index + 1U);
        }

        record->image_address =
            cluster_flash_layout_slot_address(layout, record->slot_id);
    }

    if (manager->active_slot.slot_id != 0U) {
        record = cluster_boot_find_slot(control, manager->active_slot.slot_id);
        if (record != NULL) {
            record->state = manager->active_slot.state;
            record->rollback_requested = manager->active_slot.rollback_requested ? 1U : 0U;
            record->remaining_boot_attempts =
                manager->active_slot.remaining_boot_attempts;
            record->image_size_bytes = manager->active_slot.image_size_bytes;
            record->image_crc32 = manager->active_slot.image_crc32;
            cluster_boot_copy_version(record->version, manager->active_slot.version);
            control->active_slot_id = manager->active_slot.slot_id;
        }
    }

    if (manager->rollback_slot.slot_id != 0U) {
        record = cluster_boot_find_slot(control, manager->rollback_slot.slot_id);
        if (record != NULL) {
            record->state = manager->rollback_slot.state;
            record->rollback_requested = manager->rollback_slot.rollback_requested ? 1U : 0U;
            record->remaining_boot_attempts =
                manager->rollback_slot.remaining_boot_attempts;
            record->image_size_bytes = manager->rollback_slot.image_size_bytes;
            record->image_crc32 = manager->rollback_slot.image_crc32;
            cluster_boot_copy_version(record->version, manager->rollback_slot.version);
            control->fallback_slot_id = manager->rollback_slot.slot_id;
        }
    }

    cluster_boot_control_update_crc(control);
}

void cluster_boot_control_to_ota_manager(const cluster_boot_control_block_t *control,
                                         cluster_ota_manager_t *manager) {
    const cluster_boot_slot_record_t *active_slot;
    const cluster_boot_slot_record_t *fallback_slot;

    if (control == NULL || manager == NULL) {
        return;
    }

    cluster_ota_manager_init(manager);

    active_slot = cluster_boot_find_slot_const(control, control->active_slot_id);
    fallback_slot = cluster_boot_find_slot_const(control, control->fallback_slot_id);

    if (active_slot != NULL) {
        manager->active_slot.slot_id = active_slot->slot_id;
        manager->active_slot.state = (cluster_ota_slot_state_t)active_slot->state;
        manager->active_slot.rollback_requested = active_slot->rollback_requested != 0U;
        manager->active_slot.remaining_boot_attempts =
            active_slot->remaining_boot_attempts;
        manager->active_slot.image_size_bytes = active_slot->image_size_bytes;
        manager->active_slot.image_crc32 = active_slot->image_crc32;
        cluster_boot_copy_version(manager->active_slot.version, active_slot->version);
    }

    if (fallback_slot != NULL) {
        manager->rollback_slot.slot_id = fallback_slot->slot_id;
        manager->rollback_slot.state = (cluster_ota_slot_state_t)fallback_slot->state;
        manager->rollback_slot.rollback_requested =
            fallback_slot->rollback_requested != 0U;
        manager->rollback_slot.remaining_boot_attempts =
            fallback_slot->remaining_boot_attempts;
        manager->rollback_slot.image_size_bytes = fallback_slot->image_size_bytes;
        manager->rollback_slot.image_crc32 = fallback_slot->image_crc32;
        cluster_boot_copy_version(manager->rollback_slot.version, fallback_slot->version);
    }
}
