#include "cs_journal.h"
#include "cs_crc32.h"
#include <stddef.h>
#include <string.h>

static bool cs_journal_storage_is_valid(const cs_journal_storage_t *storage) {
    return storage != NULL && storage->meta_page_size_bytes == sizeof(cs_journal_meta_t) &&
           storage->flash_page_size_bytes != 0U &&
           (storage->flash_page_size_bytes % sizeof(cs_journal_record_t)) == 0U &&
           storage->record_area_size_bytes >= sizeof(cs_journal_record_t) &&
           (storage->record_area_size_bytes % sizeof(cs_journal_record_t)) == 0U;
}

static uint32_t cs_journal_meta_crc32(const cs_journal_meta_t *meta) {
    return cs_crc32_compute(meta,
                            (uint32_t)offsetof(cs_journal_meta_t, crc32));
}

static uint32_t cs_journal_record_crc32(const cs_journal_record_t *record) {
    return cs_crc32_compute(record,
                            (uint32_t)offsetof(cs_journal_record_t, crc32));
}

static void cs_journal_meta_reset(cs_journal_meta_t *meta) {
    memset(meta, 0, sizeof(*meta));
    meta->crc32 = cs_journal_meta_crc32(meta);
}

static bool cs_journal_meta_validate(const cs_journal_meta_t *meta) {
    if (meta == NULL) {
        return false;
    }

    return cs_journal_meta_crc32(meta) == meta->crc32;
}

static void cs_journal_meta_update_crc32(cs_journal_meta_t *meta) {
    meta->crc32 = 0U;
    meta->crc32 = cs_journal_meta_crc32(meta);
}

static void cs_journal_record_update_crc32(cs_journal_record_t *record) {
    record->crc32 = 0U;
    record->crc32 = cs_journal_record_crc32(record);
}

static bool cs_journal_record_validate(const cs_journal_record_t *record) {
    return record != NULL && cs_journal_record_crc32(record) == record->crc32;
}

static uint32_t cs_journal_current_count(const cs_journal_state_t *state) {
    return state->meta.record_count > state->record_capacity
               ? state->record_capacity
               : state->meta.record_count;
}

static uint32_t cs_journal_record_address(const cs_journal_storage_t *storage,
                                          uint32_t record_index) {
    return storage->record_area_address +
           (record_index * (uint32_t)sizeof(cs_journal_record_t));
}

static uint32_t cs_journal_page_start_address(const cs_journal_storage_t *storage,
                                              uint32_t address) {
    uint32_t offset;

    offset = address - storage->record_area_address;
    return storage->record_area_address -
           (storage->record_area_address % storage->flash_page_size_bytes) +
           (offset - (offset % storage->flash_page_size_bytes));
}

static uint32_t cs_journal_meta_address(const cs_journal_storage_t *storage,
                                        cs_journal_meta_source_t source) {
    return source == CS_JOURNAL_META_SOURCE_B ? storage->meta_b_address
                                              : storage->meta_a_address;
}

static bool cs_journal_read_meta_copy(const cs_platform_t *platform,
                                      uint32_t address,
                                      cs_journal_meta_t *meta) {
    return cs_platform_flash_read(platform,
                                  address,
                                  meta,
                                  (uint32_t)sizeof(*meta)) == CS_STATUS_OK;
}

static bool cs_journal_bytes_are_erased(const uint8_t *bytes, uint32_t length) {
    uint32_t index;

    if (bytes == NULL) {
        return false;
    }

    for (index = 0U; index < length; index += 1U) {
        if (bytes[index] != 0xFFU) {
            return false;
        }
    }

    return true;
}

static bool cs_journal_store_meta(const cs_platform_t *platform,
                                  const cs_journal_storage_t *storage,
                                  cs_journal_state_t *state) {
    cs_journal_meta_t writable_meta;
    cs_journal_meta_source_t target_source;
    uint32_t target_address;

    target_source = state->active_source == CS_JOURNAL_META_SOURCE_A
                        ? CS_JOURNAL_META_SOURCE_B
                        : CS_JOURNAL_META_SOURCE_A;
    if (state->active_source == CS_JOURNAL_META_SOURCE_NONE) {
        target_source = CS_JOURNAL_META_SOURCE_A;
    }

    writable_meta = state->meta;
    writable_meta.seq += 1U;
    cs_journal_meta_update_crc32(&writable_meta);
    target_address = cs_journal_meta_address(storage, target_source);

    if (cs_platform_flash_erase(platform,
                                target_address,
                                storage->meta_page_size_bytes) != CS_STATUS_OK ||
        cs_platform_flash_write(platform,
                                target_address,
                                &writable_meta,
                                (uint32_t)sizeof(writable_meta)) != CS_STATUS_OK) {
        return false;
    }

    state->meta = writable_meta;
    state->active_source = target_source;
    return true;
}

bool cs_journal_load(const cs_platform_t *platform,
                     const cs_journal_storage_t *storage,
                     cs_journal_state_t *state) {
    cs_journal_meta_t meta_a;
    cs_journal_meta_t meta_b;
    bool meta_a_valid;
    bool meta_b_valid;

    if (platform == NULL || !cs_journal_storage_is_valid(storage) || state == NULL) {
        return false;
    }

    memset(state, 0, sizeof(*state));
    state->record_capacity =
        storage->record_area_size_bytes / (uint32_t)sizeof(cs_journal_record_t);

    meta_a_valid = cs_journal_read_meta_copy(platform, storage->meta_a_address, &meta_a) &&
                   cs_journal_meta_validate(&meta_a);
    meta_b_valid = cs_journal_read_meta_copy(platform, storage->meta_b_address, &meta_b) &&
                   cs_journal_meta_validate(&meta_b);

    if (!meta_a_valid && !meta_b_valid) {
        cs_journal_meta_reset(&state->meta);
        state->active_source = CS_JOURNAL_META_SOURCE_NONE;
        return true;
    }

    if (meta_b_valid && (!meta_a_valid || meta_b.seq >= meta_a.seq)) {
        state->meta = meta_b;
        state->active_source = CS_JOURNAL_META_SOURCE_B;
    } else {
        state->meta = meta_a;
        state->active_source = CS_JOURNAL_META_SOURCE_A;
    }

    if (state->meta.head >= state->record_capacity ||
        state->meta.tail >= state->record_capacity) {
        cs_journal_meta_reset(&state->meta);
        state->active_source = CS_JOURNAL_META_SOURCE_NONE;
    }

    return true;
}

bool cs_journal_append(const cs_platform_t *platform,
                       const cs_journal_storage_t *storage,
                       cs_journal_state_t *state,
                       const cs_journal_record_t *record) {
    cs_journal_record_t writable_record;
    cs_journal_record_t existing_record;
    cs_journal_meta_t page_buffer;
    uint32_t target_index;
    uint32_t target_address;
    uint32_t page_start_address;
    uint32_t page_offset;

    if (platform == NULL || !cs_journal_storage_is_valid(storage) || state == NULL ||
        record == NULL || state->record_capacity == 0U) {
        return false;
    }

    target_index = state->meta.tail;
    writable_record = *record;
    writable_record.seq = state->meta.record_count + 1U;
    cs_journal_record_update_crc32(&writable_record);

    target_address = cs_journal_record_address(storage, target_index);
    if (cs_platform_flash_read(platform,
                               target_address,
                               &existing_record,
                               (uint32_t)sizeof(existing_record)) != CS_STATUS_OK) {
        return false;
    }

    if (cs_journal_bytes_are_erased((const uint8_t *)&existing_record,
                                    (uint32_t)sizeof(existing_record))) {
        if (cs_platform_flash_write(platform,
                                    target_address,
                                    &writable_record,
                                    (uint32_t)sizeof(writable_record)) != CS_STATUS_OK) {
            return false;
        }
    } else {
        if (storage->flash_page_size_bytes > sizeof(page_buffer)) {
            return false;
        }

        page_start_address = cs_journal_page_start_address(storage, target_address);
        if (cs_platform_flash_read(platform,
                                   page_start_address,
                                   &page_buffer,
                                   storage->flash_page_size_bytes) != CS_STATUS_OK) {
            return false;
        }

        page_offset = target_address - page_start_address;
        memcpy(((uint8_t *)&page_buffer) + page_offset,
               &writable_record,
               sizeof(writable_record));

        if (cs_platform_flash_erase(platform,
                                    page_start_address,
                                    storage->flash_page_size_bytes) != CS_STATUS_OK) {
            return false;
        }

        if (cs_platform_flash_write(platform,
                                    page_start_address,
                                    &page_buffer,
                                    storage->flash_page_size_bytes) != CS_STATUS_OK) {
            return false;
        }
    }

    if (state->meta.record_count < state->record_capacity) {
        state->meta.record_count += 1U;
    } else {
        state->meta.record_count += 1U;
        state->meta.head = (state->meta.head + 1U) % state->record_capacity;
    }
    state->meta.tail = (state->meta.tail + 1U) % state->record_capacity;
    return cs_journal_store_meta(platform, storage, state);
}

bool cs_journal_read(const cs_platform_t *platform,
                     const cs_journal_storage_t *storage,
                     const cs_journal_state_t *state,
                     uint32_t index_from_oldest,
                     cs_journal_record_t *record) {
    uint32_t current_count;
    uint32_t record_index;

    if (platform == NULL || !cs_journal_storage_is_valid(storage) || state == NULL ||
        record == NULL) {
        return false;
    }

    current_count = cs_journal_current_count(state);
    if (index_from_oldest >= current_count) {
        return false;
    }

    record_index = (state->meta.head + index_from_oldest) % state->record_capacity;
    if (cs_platform_flash_read(platform,
                               cs_journal_record_address(storage, record_index),
                               record,
                               (uint32_t)sizeof(*record)) != CS_STATUS_OK) {
        return false;
    }

    return cs_journal_record_validate(record);
}

bool cs_journal_latest(const cs_platform_t *platform,
                       const cs_journal_storage_t *storage,
                       const cs_journal_state_t *state,
                       cs_journal_record_t *record) {
    uint32_t current_count;

    if (state == NULL) {
        return false;
    }

    current_count = cs_journal_current_count(state);
    if (current_count == 0U) {
        return false;
    }

    return cs_journal_read(platform, storage, state, current_count - 1U, record);
}
