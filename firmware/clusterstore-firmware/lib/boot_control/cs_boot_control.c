#include "cs_boot_control.h"
#include "cs_crc32.h"
#include <stddef.h>
#include <string.h>

static bool cs_bcb_storage_is_valid(const cs_bcb_storage_t *storage) {
    return storage != NULL && storage->primary_address != 0U &&
           storage->shadow_address != 0U &&
           storage->page_size_bytes >= sizeof(cs_boot_control_block_t);
}

static uint8_t cs_bcb_other_slot(uint8_t slot) {
    return slot == CS_BCB_SLOT_A ? CS_BCB_SLOT_B : CS_BCB_SLOT_A;
}

static uint32_t cs_bcb_crc32(const cs_boot_control_block_t *bcb) {
    return cs_crc32_compute(bcb,
                            (uint32_t)offsetof(cs_boot_control_block_t, crc32));
}

static bool cs_bcb_read_copy(const cs_platform_t *platform,
                             uint32_t address,
                             cs_boot_control_block_t *bcb) {
    return cs_platform_flash_read(platform,
                                  address,
                                  bcb,
                                  (uint32_t)sizeof(*bcb)) == CS_STATUS_OK;
}

static uint32_t cs_bcb_source_address(const cs_bcb_storage_t *storage,
                                      cs_bcb_source_t source) {
    if (source == CS_BCB_SOURCE_PRIMARY) {
        return storage->primary_address;
    }

    if (source == CS_BCB_SOURCE_SHADOW) {
        return storage->shadow_address;
    }

    return 0U;
}

void cs_bcb_init(cs_boot_control_block_t *bcb,
                 uint8_t active_slot,
                 uint32_t confirmed_version) {
    uint8_t index;

    if (bcb == NULL || active_slot >= CS_BCB_SLOT_COUNT) {
        return;
    }

    memset(bcb, 0, sizeof(*bcb));
    bcb->magic = CS_BCB_MAGIC;
    bcb->active_slot = active_slot;
    bcb->confirmed_version = confirmed_version;
    bcb->candidate_version = 0U;
    bcb->trial_boots_remaining = 0U;
    bcb->rollback_requested = 0U;

    for (index = 0U; index < CS_BCB_SLOT_COUNT; index += 1U) {
        bcb->slot_state[index] = CS_SLOT_EMPTY;
    }

    bcb->slot_state[active_slot] = CS_SLOT_CONFIRMED;
    bcb->slot_state[cs_bcb_other_slot(active_slot)] = CS_SLOT_EMPTY;
    cs_bcb_update_crc32(bcb);
}

bool cs_bcb_validate(const cs_boot_control_block_t *bcb) {
    if (bcb == NULL || bcb->magic != CS_BCB_MAGIC ||
        bcb->active_slot >= CS_BCB_SLOT_COUNT) {
        return false;
    }

    return cs_bcb_crc32(bcb) == bcb->crc32;
}

void cs_bcb_update_crc32(cs_boot_control_block_t *bcb) {
    if (bcb == NULL) {
        return;
    }

    bcb->crc32 = 0U;
    bcb->crc32 = cs_bcb_crc32(bcb);
}

bool cs_bcb_read(const cs_platform_t *platform,
                 const cs_bcb_storage_t *storage,
                 cs_boot_control_block_t *bcb,
                 cs_bcb_source_t *source_out) {
    cs_boot_control_block_t primary_copy;
    cs_boot_control_block_t shadow_copy;
    bool primary_valid;
    bool shadow_valid;

    if (platform == NULL || !cs_bcb_storage_is_valid(storage) || bcb == NULL) {
        return false;
    }

    primary_valid = cs_bcb_read_copy(platform, storage->primary_address, &primary_copy) &&
                    cs_bcb_validate(&primary_copy);
    shadow_valid = cs_bcb_read_copy(platform, storage->shadow_address, &shadow_copy) &&
                   cs_bcb_validate(&shadow_copy);

    if (!primary_valid && !shadow_valid) {
        return false;
    }

    if (shadow_valid && (!primary_valid || shadow_copy.seq >= primary_copy.seq)) {
        *bcb = shadow_copy;
        if (source_out != NULL) {
            *source_out = CS_BCB_SOURCE_SHADOW;
        }
        return true;
    }

    *bcb = primary_copy;
    if (source_out != NULL) {
        *source_out = CS_BCB_SOURCE_PRIMARY;
    }
    return true;
}

bool cs_bcb_store(const cs_platform_t *platform,
                  const cs_bcb_storage_t *storage,
                  cs_boot_control_block_t *bcb,
                  cs_bcb_source_t last_source,
                  cs_bcb_source_t *stored_source_out) {
    cs_boot_control_block_t writable_copy;
    cs_bcb_source_t target_source;
    uint32_t target_address;
    uint32_t previous_seq;
    uint32_t previous_crc32;

    if (platform == NULL || !cs_bcb_storage_is_valid(storage) || bcb == NULL) {
        return false;
    }

    target_source = last_source == CS_BCB_SOURCE_PRIMARY ? CS_BCB_SOURCE_SHADOW
                                                         : CS_BCB_SOURCE_PRIMARY;
    if (last_source == CS_BCB_SOURCE_NONE) {
        target_source = CS_BCB_SOURCE_PRIMARY;
    }

    target_address = cs_bcb_source_address(storage, target_source);
    previous_seq = bcb->seq;
    previous_crc32 = bcb->crc32;
    bcb->seq += 1U;
    cs_bcb_update_crc32(bcb);
    writable_copy = *bcb;

    if (cs_platform_flash_erase(platform,
                                target_address,
                                storage->page_size_bytes) != CS_STATUS_OK ||
        cs_platform_flash_write(platform,
                                target_address,
                                &writable_copy,
                                (uint32_t)sizeof(writable_copy)) != CS_STATUS_OK) {
        bcb->seq = previous_seq;
        bcb->crc32 = previous_crc32;
        return false;
    }

    if (stored_source_out != NULL) {
        *stored_source_out = target_source;
    }
    return true;
}

bool cs_bcb_register_candidate(cs_boot_control_block_t *bcb,
                               uint8_t slot,
                               uint32_t version,
                               uint8_t trial_boots) {
    if (bcb == NULL || slot >= CS_BCB_SLOT_COUNT || trial_boots == 0U ||
        version == 0U) {
        return false;
    }

    bcb->active_slot = slot;
    bcb->slot_state[slot] = CS_SLOT_CANDIDATE;
    bcb->candidate_version = version;
    bcb->trial_boots_remaining = trial_boots;
    bcb->rollback_requested = 0U;
    cs_bcb_update_crc32(bcb);
    return true;
}

bool cs_bcb_confirm(cs_boot_control_block_t *bcb) {
    if (bcb == NULL || bcb->active_slot >= CS_BCB_SLOT_COUNT) {
        return false;
    }

    bcb->slot_state[bcb->active_slot] = CS_SLOT_CONFIRMED;
    bcb->confirmed_version = bcb->candidate_version != 0U ? bcb->candidate_version
                                                           : bcb->confirmed_version;
    bcb->candidate_version = 0U;
    bcb->trial_boots_remaining = 0U;
    bcb->rollback_requested = 0U;
    cs_bcb_update_crc32(bcb);
    return true;
}

bool cs_bcb_request_rollback(cs_boot_control_block_t *bcb) {
    if (bcb == NULL) {
        return false;
    }

    bcb->rollback_requested = 1U;
    cs_bcb_update_crc32(bcb);
    return true;
}

bool cs_bcb_mark_bad(cs_boot_control_block_t *bcb, uint8_t slot) {
    if (bcb == NULL || slot >= CS_BCB_SLOT_COUNT) {
        return false;
    }

    bcb->slot_state[slot] = CS_SLOT_BAD;
    if (bcb->active_slot == slot) {
        bcb->candidate_version = 0U;
        bcb->trial_boots_remaining = 0U;
        bcb->rollback_requested = 1U;
    }
    cs_bcb_update_crc32(bcb);
    return true;
}

cs_boot_decision_t cs_bcb_select_slot(cs_boot_control_block_t *bcb,
                                      bool consume_trial_boot,
                                      uint8_t *slot_out) {
    uint8_t active_slot;
    uint8_t fallback_slot;

    if (bcb == NULL || slot_out == NULL || bcb->active_slot >= CS_BCB_SLOT_COUNT) {
        return CS_BOOT_DECISION_STAY_IN_BOOTLOADER;
    }

    active_slot = bcb->active_slot;
    fallback_slot = cs_bcb_other_slot(active_slot);

    if (bcb->rollback_requested != 0U ||
        bcb->slot_state[active_slot] == CS_SLOT_BAD ||
        bcb->slot_state[active_slot] == CS_SLOT_EMPTY ||
        (bcb->slot_state[active_slot] == CS_SLOT_CANDIDATE &&
         bcb->trial_boots_remaining == 0U)) {
        if (bcb->slot_state[fallback_slot] == CS_SLOT_CONFIRMED ||
            bcb->slot_state[fallback_slot] == CS_SLOT_ACTIVE) {
            if (bcb->slot_state[active_slot] == CS_SLOT_CANDIDATE ||
                bcb->rollback_requested != 0U) {
                bcb->slot_state[active_slot] = CS_SLOT_BAD;
                bcb->candidate_version = 0U;
            }
            bcb->active_slot = fallback_slot;
            bcb->rollback_requested = 0U;
            bcb->trial_boots_remaining = 0U;
            cs_bcb_update_crc32(bcb);
            *slot_out = fallback_slot;
            return CS_BOOT_DECISION_BOOT_FALLBACK;
        }

        return CS_BOOT_DECISION_STAY_IN_BOOTLOADER;
    }

    if (bcb->slot_state[active_slot] == CS_SLOT_CANDIDATE) {
        if (consume_trial_boot && bcb->trial_boots_remaining > 0U) {
            bcb->trial_boots_remaining -= 1U;
            cs_bcb_update_crc32(bcb);
        }
        *slot_out = active_slot;
        return CS_BOOT_DECISION_BOOT_ACTIVE;
    }

    if (bcb->slot_state[active_slot] == CS_SLOT_CONFIRMED ||
        bcb->slot_state[active_slot] == CS_SLOT_ACTIVE) {
        *slot_out = active_slot;
        return CS_BOOT_DECISION_BOOT_ACTIVE;
    }

    return CS_BOOT_DECISION_STAY_IN_BOOTLOADER;
}

static uint32_t cs_image_header_crc32(const cs_image_header_t *header) {
    return cs_crc32_compute(header,
                            (uint32_t)offsetof(cs_image_header_t, header_crc32));
}

void cs_image_header_init(cs_image_header_t *header,
                          uint32_t version,
                          uint32_t image_size,
                          uint32_t image_crc32) {
    if (header == NULL) {
        return;
    }

    memset(header, 0, sizeof(*header));
    header->magic = CS_IMAGE_MAGIC;
    header->version = version;
    header->image_size = image_size;
    header->image_crc32 = image_crc32;
    header->sig_type = CS_SIG_TYPE_NONE;
    cs_image_header_update_crc32(header);
}

bool cs_image_header_validate(const cs_image_header_t *header) {
    if (header == NULL || header->magic != CS_IMAGE_MAGIC ||
        header->image_size == 0U) {
        return false;
    }

    if (header->sig_type != CS_SIG_TYPE_NONE &&
        header->sig_type != CS_SIG_TYPE_ED25519) {
        return false;
    }

    return cs_image_header_crc32(header) == header->header_crc32;
}

void cs_image_header_update_crc32(cs_image_header_t *header) {
    if (header == NULL) {
        return;
    }

    header->header_crc32 = 0U;
    header->header_crc32 = cs_image_header_crc32(header);
}
