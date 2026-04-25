#include "cluster_flash_layout.h"
#include <stddef.h>

bool cluster_flash_layout_build(const cluster_flash_layout_config_t *config,
                                cluster_flash_layout_t *layout) {
    uint32_t usable_bytes;
    uint32_t slot_size_bytes;
    uint32_t metadata_address;
    uint32_t journal_address;
    uint32_t end_address;

    if (config == NULL || layout == NULL || config->flash_size_bytes == 0U) {
        return false;
    }

    if (config->flash_size_bytes <= config->bootloader_size_bytes +
                                        config->metadata_size_bytes +
                                        config->journal_size_bytes) {
        return false;
    }

    usable_bytes = config->flash_size_bytes - config->bootloader_size_bytes -
                   config->metadata_size_bytes - config->journal_size_bytes;
    slot_size_bytes = config->slot_size_bytes == 0U
                          ? (usable_bytes / 2U)
                          : config->slot_size_bytes;

    if (slot_size_bytes == 0U || (slot_size_bytes * 2U) > usable_bytes) {
        return false;
    }

    layout->bootloader_address = config->flash_base_address;
    layout->slot_a_address = config->flash_base_address + config->bootloader_size_bytes;
    layout->slot_b_address = layout->slot_a_address + slot_size_bytes;

    metadata_address = layout->slot_b_address + slot_size_bytes;
    journal_address = metadata_address + config->metadata_size_bytes;
    end_address = config->flash_base_address + config->flash_size_bytes;

    if (journal_address + config->journal_size_bytes > end_address) {
        return false;
    }

    layout->metadata_address = metadata_address;
    layout->journal_address = journal_address;
    layout->end_address = end_address;
    layout->slot_size_bytes = slot_size_bytes;
    layout->metadata_size_bytes = config->metadata_size_bytes;
    layout->journal_size_bytes = config->journal_size_bytes;
    return true;
}

bool cluster_flash_layout_is_valid(const cluster_flash_layout_t *layout) {
    if (layout == NULL || layout->slot_size_bytes == 0U ||
        layout->metadata_size_bytes == 0U || layout->journal_size_bytes == 0U) {
        return false;
    }

    if (layout->slot_a_address < layout->bootloader_address ||
        layout->slot_b_address <= layout->slot_a_address ||
        layout->metadata_address <= layout->slot_b_address ||
        layout->journal_address <= layout->metadata_address ||
        layout->end_address <= layout->journal_address) {
        return false;
    }

    return true;
}

uint32_t cluster_flash_layout_slot_address(const cluster_flash_layout_t *layout,
                                           uint8_t slot_id) {
    if (layout == NULL) {
        return 0U;
    }

    if (slot_id == CLUSTER_BOOT_SLOT_A) {
        return layout->slot_a_address;
    }

    if (slot_id == CLUSTER_BOOT_SLOT_B) {
        return layout->slot_b_address;
    }

    return 0U;
}
