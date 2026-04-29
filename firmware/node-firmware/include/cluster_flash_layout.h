#ifndef CLUSTER_FLASH_LAYOUT_H
#define CLUSTER_FLASH_LAYOUT_H

#include <stdbool.h>
#include <stdint.h>

#define CLUSTER_BOOT_SLOT_A 1U
#define CLUSTER_BOOT_SLOT_B 2U

typedef struct {
    uint32_t flash_base_address;
    uint32_t flash_size_bytes;
    uint32_t bootloader_size_bytes;
    uint32_t metadata_size_bytes;
    uint32_t journal_size_bytes;
    uint32_t slot_size_bytes;
    uint32_t bootloader_address;
    uint32_t slot_a_address;
    uint32_t slot_b_address;
    uint32_t metadata_address;
    uint32_t journal_address;
} cluster_flash_layout_config_t;

typedef struct {
    uint32_t bootloader_address;
    uint32_t slot_a_address;
    uint32_t slot_b_address;
    uint32_t metadata_address;
    uint32_t journal_address;
    uint32_t end_address;
    uint32_t bootloader_size_bytes;
    uint32_t slot_size_bytes;
    uint32_t metadata_size_bytes;
    uint32_t journal_size_bytes;
} cluster_flash_layout_t;

bool cluster_flash_layout_build(const cluster_flash_layout_config_t *config,
                                cluster_flash_layout_t *layout);
bool cluster_flash_layout_is_valid(const cluster_flash_layout_t *layout);
uint32_t cluster_flash_layout_slot_address(const cluster_flash_layout_t *layout,
                                           uint8_t slot_id);

#endif
