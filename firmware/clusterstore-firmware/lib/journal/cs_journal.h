#ifndef CS_JOURNAL_H
#define CS_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>
#include "cs_cluster_platform.h"

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t timestamp_ms;
    uint16_t event_code;
    uint16_t node_id;
    int32_t value_a;
    int32_t value_b;
    uint8_t severity;
    uint8_t reserved[7];
    uint32_t crc32;
} cs_journal_record_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t head;
    uint32_t tail;
    uint32_t record_count;
    uint32_t crc32;
    uint8_t reserved[2028];
} cs_journal_meta_t;

typedef enum {
    CS_JOURNAL_META_SOURCE_NONE = 0,
    CS_JOURNAL_META_SOURCE_A = 1,
    CS_JOURNAL_META_SOURCE_B = 2
} cs_journal_meta_source_t;

typedef struct {
    uint32_t meta_a_address;
    uint32_t meta_b_address;
    uint32_t record_area_address;
    uint32_t meta_page_size_bytes;
    uint32_t record_area_size_bytes;
    uint32_t flash_page_size_bytes;
} cs_journal_storage_t;

typedef struct {
    cs_journal_meta_t meta;
    uint32_t record_capacity;
    cs_journal_meta_source_t active_source;
} cs_journal_state_t;

_Static_assert(sizeof(cs_journal_record_t) == 32U,
               "cs_journal_record_t size drift");
_Static_assert(sizeof(cs_journal_meta_t) == 2048U,
               "cs_journal_meta_t size drift");

bool cs_journal_load(const cs_platform_t *platform,
                     const cs_journal_storage_t *storage,
                     cs_journal_state_t *state);
bool cs_journal_append(const cs_platform_t *platform,
                       const cs_journal_storage_t *storage,
                       cs_journal_state_t *state,
                       const cs_journal_record_t *record);
bool cs_journal_read(const cs_platform_t *platform,
                     const cs_journal_storage_t *storage,
                     const cs_journal_state_t *state,
                     uint32_t index_from_oldest,
                     cs_journal_record_t *record);
bool cs_journal_latest(const cs_platform_t *platform,
                       const cs_journal_storage_t *storage,
                       const cs_journal_state_t *state,
                       cs_journal_record_t *record);

#endif
