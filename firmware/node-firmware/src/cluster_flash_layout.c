#include "cluster_flash_layout.h"
#include <stddef.h>

static bool cluster_flash_region_in_range(uint32_t flash_base_address,
                                          uint32_t flash_size_bytes,
                                          uint32_t address,
                                          uint32_t size_bytes) {
    return size_bytes != 0U && address >= flash_base_address &&
           size_bytes <= flash_size_bytes &&
           (address - flash_base_address) <= (flash_size_bytes - size_bytes);
}

static bool cluster_flash_regions_overlap(uint32_t first_address,
                                          uint32_t first_size_bytes,
                                          uint32_t second_address,
                                          uint32_t second_size_bytes) {
    return first_address < (second_address + second_size_bytes) &&
           second_address < (first_address + first_size_bytes);
}

static bool cluster_flash_layout_build_explicit(
    const cluster_flash_layout_config_t *config,
    cluster_flash_layout_t *layout) {
    uint32_t flash_end_address;

    if (config->bootloader_address == 0U || config->slot_a_address == 0U ||
        config->slot_b_address == 0U || config->metadata_address == 0U ||
        config->journal_address == 0U || config->bootloader_size_bytes == 0U ||
        config->slot_size_bytes == 0U || config->metadata_size_bytes == 0U ||
        config->journal_size_bytes == 0U) {
        return false;
    }

    flash_end_address = config->flash_base_address + config->flash_size_bytes;
    if (!cluster_flash_region_in_range(config->flash_base_address,
                                       config->flash_size_bytes,
                                       config->bootloader_address,
                                       config->bootloader_size_bytes) ||
        !cluster_flash_region_in_range(config->flash_base_address,
                                       config->flash_size_bytes,
                                       config->slot_a_address,
                                       config->slot_size_bytes) ||
        !cluster_flash_region_in_range(config->flash_base_address,
                                       config->flash_size_bytes,
                                       config->slot_b_address,
                                       config->slot_size_bytes) ||
        !cluster_flash_region_in_range(config->flash_base_address,
                                       config->flash_size_bytes,
                                       config->metadata_address,
                                       config->metadata_size_bytes) ||
        !cluster_flash_region_in_range(config->flash_base_address,
                                       config->flash_size_bytes,
                                       config->journal_address,
                                       config->journal_size_bytes)) {
        return false;
    }

    if (cluster_flash_regions_overlap(config->bootloader_address,
                                      config->bootloader_size_bytes,
                                      config->slot_a_address,
                                      config->slot_size_bytes) ||
        cluster_flash_regions_overlap(config->bootloader_address,
                                      config->bootloader_size_bytes,
                                      config->slot_b_address,
                                      config->slot_size_bytes) ||
        cluster_flash_regions_overlap(config->bootloader_address,
                                      config->bootloader_size_bytes,
                                      config->metadata_address,
                                      config->metadata_size_bytes) ||
        cluster_flash_regions_overlap(config->bootloader_address,
                                      config->bootloader_size_bytes,
                                      config->journal_address,
                                      config->journal_size_bytes) ||
        cluster_flash_regions_overlap(config->slot_a_address,
                                      config->slot_size_bytes,
                                      config->slot_b_address,
                                      config->slot_size_bytes) ||
        cluster_flash_regions_overlap(config->slot_a_address,
                                      config->slot_size_bytes,
                                      config->metadata_address,
                                      config->metadata_size_bytes) ||
        cluster_flash_regions_overlap(config->slot_a_address,
                                      config->slot_size_bytes,
                                      config->journal_address,
                                      config->journal_size_bytes) ||
        cluster_flash_regions_overlap(config->slot_b_address,
                                      config->slot_size_bytes,
                                      config->metadata_address,
                                      config->metadata_size_bytes) ||
        cluster_flash_regions_overlap(config->slot_b_address,
                                      config->slot_size_bytes,
                                      config->journal_address,
                                      config->journal_size_bytes) ||
        cluster_flash_regions_overlap(config->metadata_address,
                                      config->metadata_size_bytes,
                                      config->journal_address,
                                      config->journal_size_bytes)) {
        return false;
    }

    layout->bootloader_address = config->bootloader_address;
    layout->slot_a_address = config->slot_a_address;
    layout->slot_b_address = config->slot_b_address;
    layout->metadata_address = config->metadata_address;
    layout->journal_address = config->journal_address;
    layout->end_address = flash_end_address;
    layout->bootloader_size_bytes = config->bootloader_size_bytes;
    layout->slot_size_bytes = config->slot_size_bytes;
    layout->metadata_size_bytes = config->metadata_size_bytes;
    layout->journal_size_bytes = config->journal_size_bytes;
    return true;
}

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

    if (config->bootloader_address != 0U || config->slot_a_address != 0U ||
        config->slot_b_address != 0U || config->metadata_address != 0U ||
        config->journal_address != 0U) {
        return cluster_flash_layout_build_explicit(config, layout);
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
    layout->bootloader_size_bytes = config->bootloader_size_bytes;
    layout->slot_size_bytes = slot_size_bytes;
    layout->metadata_size_bytes = config->metadata_size_bytes;
    layout->journal_size_bytes = config->journal_size_bytes;
    return true;
}

bool cluster_flash_layout_is_valid(const cluster_flash_layout_t *layout) {
    if (layout == NULL || layout->slot_size_bytes == 0U ||
        layout->metadata_size_bytes == 0U || layout->journal_size_bytes == 0U ||
        layout->bootloader_size_bytes == 0U) {
        return false;
    }

    if (layout->end_address <= layout->bootloader_address) {
        return false;
    }

    if (!cluster_flash_region_in_range(layout->bootloader_address,
                                       layout->end_address -
                                           layout->bootloader_address,
                                       layout->bootloader_address,
                                       layout->bootloader_size_bytes) ||
        !cluster_flash_region_in_range(layout->bootloader_address,
                                       layout->end_address -
                                           layout->bootloader_address,
                                       layout->slot_a_address,
                                       layout->slot_size_bytes) ||
        !cluster_flash_region_in_range(layout->bootloader_address,
                                       layout->end_address -
                                           layout->bootloader_address,
                                       layout->slot_b_address,
                                       layout->slot_size_bytes) ||
        !cluster_flash_region_in_range(layout->bootloader_address,
                                       layout->end_address -
                                           layout->bootloader_address,
                                       layout->metadata_address,
                                       layout->metadata_size_bytes) ||
        !cluster_flash_region_in_range(layout->bootloader_address,
                                       layout->end_address -
                                           layout->bootloader_address,
                                       layout->journal_address,
                                       layout->journal_size_bytes)) {
        return false;
    }

    if (cluster_flash_regions_overlap(layout->bootloader_address,
                                      layout->bootloader_size_bytes,
                                      layout->slot_a_address,
                                      layout->slot_size_bytes) ||
        cluster_flash_regions_overlap(layout->bootloader_address,
                                      layout->bootloader_size_bytes,
                                      layout->slot_b_address,
                                      layout->slot_size_bytes) ||
        cluster_flash_regions_overlap(layout->bootloader_address,
                                      layout->bootloader_size_bytes,
                                      layout->metadata_address,
                                      layout->metadata_size_bytes) ||
        cluster_flash_regions_overlap(layout->bootloader_address,
                                      layout->bootloader_size_bytes,
                                      layout->journal_address,
                                      layout->journal_size_bytes) ||
        cluster_flash_regions_overlap(layout->slot_a_address,
                                      layout->slot_size_bytes,
                                      layout->slot_b_address,
                                      layout->slot_size_bytes) ||
        cluster_flash_regions_overlap(layout->slot_a_address,
                                      layout->slot_size_bytes,
                                      layout->metadata_address,
                                      layout->metadata_size_bytes) ||
        cluster_flash_regions_overlap(layout->slot_a_address,
                                      layout->slot_size_bytes,
                                      layout->journal_address,
                                      layout->journal_size_bytes) ||
        cluster_flash_regions_overlap(layout->slot_b_address,
                                      layout->slot_size_bytes,
                                      layout->metadata_address,
                                      layout->metadata_size_bytes) ||
        cluster_flash_regions_overlap(layout->slot_b_address,
                                      layout->slot_size_bytes,
                                      layout->journal_address,
                                      layout->journal_size_bytes) ||
        cluster_flash_regions_overlap(layout->metadata_address,
                                      layout->metadata_size_bytes,
                                      layout->journal_address,
                                      layout->journal_size_bytes)) {
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
