#ifndef CLUSTER_PERSISTENT_STATE_H
#define CLUSTER_PERSISTENT_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_boot_control.h"
#include "cluster_event_journal.h"
#include "cluster_flash_layout.h"
#include "cluster_platform.h"

#define CLUSTER_PERSISTENT_STATE_MAGIC 0x43505354UL
#define CLUSTER_PERSISTENT_STATE_VERSION 1U
#define CLUSTER_PERSISTENT_STATE_COPY_COUNT 2U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length_bytes;
    uint32_t generation;
    cluster_boot_control_block_t boot_control;
    cluster_event_journal_metadata_t journal_metadata;
    uint16_t journal_capacity;
    uint16_t reserved;
    uint32_t crc32;
} cluster_persistent_state_image_t;

typedef struct {
    const cluster_platform_t *platform;
    const cluster_flash_layout_t *layout;
    uint16_t journal_capacity;
    cluster_boot_control_block_t boot_control;
    cluster_event_journal_metadata_t journal_metadata;
    uint32_t generation;
    uint8_t active_copy_index;
} cluster_persistent_state_t;

void cluster_persistent_state_init(cluster_persistent_state_t *state,
                                   const cluster_platform_t *platform,
                                   const cluster_flash_layout_t *layout,
                                   uint16_t journal_capacity);
uint32_t cluster_persistent_state_required_metadata_bytes(void);
bool cluster_persistent_state_is_layout_compatible(
    const cluster_flash_layout_t *layout,
    uint16_t journal_capacity);
bool cluster_persistent_state_load(cluster_persistent_state_t *state,
                                   uint8_t default_active_slot_id,
                                   const char *default_version);
bool cluster_persistent_state_save(cluster_persistent_state_t *state);
bool cluster_persistent_state_restore_journal(cluster_persistent_state_t *state,
                                              cluster_event_journal_t *journal);
bool cluster_persistent_state_flush_journal(
    void *context,
    const cluster_event_record_t *records,
    uint16_t capacity,
    const cluster_event_journal_metadata_t *metadata);

#endif
