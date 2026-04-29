#ifndef CS_BOOT_CONTROL_H
#define CS_BOOT_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "cs_cluster_platform.h"

#define CS_BCB_MAGIC 0x43534243UL
#define CS_BCB_SLOT_A 0U
#define CS_BCB_SLOT_B 1U
#define CS_BCB_SLOT_COUNT 2U

#define CS_IMAGE_MAGIC 0xC5031E2EUL
#define CS_SIG_TYPE_NONE 0x00U
#define CS_SIG_TYPE_ED25519 0x01U

typedef enum {
    CS_SLOT_EMPTY = 0xFF,
    CS_SLOT_CANDIDATE = 0x01,
    CS_SLOT_CONFIRMED = 0x02,
    CS_SLOT_BAD = 0x03,
    CS_SLOT_ACTIVE = 0x04
} cs_slot_state_t;

typedef enum {
    CS_BCB_SOURCE_NONE = 0,
    CS_BCB_SOURCE_PRIMARY = 1,
    CS_BCB_SOURCE_SHADOW = 2
} cs_bcb_source_t;

typedef enum {
    CS_BOOT_DECISION_BOOT_ACTIVE = 0,
    CS_BOOT_DECISION_BOOT_FALLBACK = 1,
    CS_BOOT_DECISION_STAY_IN_BOOTLOADER = 2
} cs_boot_decision_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint8_t active_slot;
    uint8_t slot_state[CS_BCB_SLOT_COUNT];
    uint8_t trial_boots_remaining;
    uint32_t confirmed_version;
    uint32_t candidate_version;
    uint8_t rollback_requested;
    uint8_t reserved[15];
    uint32_t crc32;
} cs_boot_control_block_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t header_crc32;
    uint32_t image_crc32;
    uint8_t sig_type;
    uint8_t reserved[3];
    uint8_t signature[64];
    uint8_t padding[40];
} cs_image_header_t;

typedef struct {
    uint32_t primary_address;
    uint32_t shadow_address;
    uint32_t page_size_bytes;
} cs_bcb_storage_t;

_Static_assert(sizeof(cs_boot_control_block_t) == 40U,
               "cs_boot_control_block_t size drift");
_Static_assert(sizeof(cs_image_header_t) == 128U,
               "cs_image_header_t size drift");

void cs_bcb_init(cs_boot_control_block_t *bcb,
                 uint8_t active_slot,
                 uint32_t confirmed_version);
bool cs_bcb_validate(const cs_boot_control_block_t *bcb);
void cs_bcb_update_crc32(cs_boot_control_block_t *bcb);
bool cs_bcb_read(const cs_platform_t *platform,
                 const cs_bcb_storage_t *storage,
                 cs_boot_control_block_t *bcb,
                 cs_bcb_source_t *source_out);
bool cs_bcb_store(const cs_platform_t *platform,
                  const cs_bcb_storage_t *storage,
                  cs_boot_control_block_t *bcb,
                  cs_bcb_source_t last_source,
                  cs_bcb_source_t *stored_source_out);
bool cs_bcb_register_candidate(cs_boot_control_block_t *bcb,
                               uint8_t slot,
                               uint32_t version,
                               uint8_t trial_boots);
bool cs_bcb_confirm(cs_boot_control_block_t *bcb);
bool cs_bcb_request_rollback(cs_boot_control_block_t *bcb);
bool cs_bcb_mark_bad(cs_boot_control_block_t *bcb, uint8_t slot);
cs_boot_decision_t cs_bcb_select_slot(cs_boot_control_block_t *bcb,
                                      bool consume_trial_boot,
                                      uint8_t *slot_out);

void cs_image_header_init(cs_image_header_t *header,
                          uint32_t version,
                          uint32_t image_size,
                          uint32_t image_crc32);
bool cs_image_header_validate(const cs_image_header_t *header);
void cs_image_header_update_crc32(cs_image_header_t *header);

#endif
