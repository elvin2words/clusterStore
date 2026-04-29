#include "cs_journal.h"
#include "cs_test.h"
#include "flash_sim.h"
#include <string.h>

static void fill_record(cs_journal_record_t *record,
                        uint32_t timestamp_ms,
                        uint16_t event_code,
                        uint16_t node_id,
                        int32_t value_a) {
    memset(record, 0, sizeof(*record));
    record->timestamp_ms = timestamp_ms;
    record->event_code = event_code;
    record->node_id = node_id;
    record->value_a = value_a;
    record->value_b = value_a * 2;
    record->severity = 2U;
}

static int test_journal_wrap_and_latest(void) {
    cs_flash_sim_t flash;
    cs_platform_t platform;
    cs_journal_storage_t storage;
    cs_journal_state_t state;
    cs_journal_record_t record;
    cs_journal_record_t loaded;
    uint32_t index;

    CS_TEST_ASSERT_TRUE(cs_flash_sim_init(&flash, 0x0800A000UL, 0x6000UL, 0x800UL));
    cs_flash_sim_bind_platform(&flash, &platform);
    storage.meta_a_address = 0x0800A000UL;
    storage.meta_b_address = 0x0800A800UL;
    storage.record_area_address = 0x0800B000UL;
    storage.meta_page_size_bytes = 0x800UL;
    storage.record_area_size_bytes = 0x1000UL;
    storage.flash_page_size_bytes = 0x800UL;

    CS_TEST_ASSERT_TRUE(cs_journal_load(&platform, &storage, &state));
    CS_TEST_ASSERT_EQ_U32(state.record_capacity, 128U);

    for (index = 0U; index < 130U; index += 1U) {
        fill_record(&record, 1000U + index, 10U + (uint16_t)index, 1U, (int32_t)index);
        CS_TEST_ASSERT_TRUE(cs_journal_append(&platform, &storage, &state, &record));
    }

    CS_TEST_ASSERT_TRUE(cs_journal_read(&platform, &storage, &state, 0U, &loaded));
    CS_TEST_ASSERT_EQ_U32(loaded.seq, 3U);
    CS_TEST_ASSERT_EQ_U32(loaded.value_a, 2U);

    CS_TEST_ASSERT_TRUE(cs_journal_latest(&platform, &storage, &state, &loaded));
    CS_TEST_ASSERT_EQ_U32(loaded.seq, 130U);
    CS_TEST_ASSERT_EQ_U32(loaded.value_a, 129U);

    cs_flash_sim_deinit(&flash);
    return EXIT_SUCCESS;
}

static int test_journal_recovers_from_corrupt_newer_meta_page(void) {
    cs_flash_sim_t flash;
    cs_platform_t platform;
    cs_journal_storage_t storage;
    cs_journal_state_t initial_state;
    cs_journal_state_t recovered_state;
    cs_journal_record_t record;
    uint8_t *meta_b_bytes;

    CS_TEST_ASSERT_TRUE(cs_flash_sim_init(&flash, 0x0800A000UL, 0x6000UL, 0x800UL));
    cs_flash_sim_bind_platform(&flash, &platform);
    storage.meta_a_address = 0x0800A000UL;
    storage.meta_b_address = 0x0800A800UL;
    storage.record_area_address = 0x0800B000UL;
    storage.meta_page_size_bytes = 0x800UL;
    storage.record_area_size_bytes = 0x1000UL;
    storage.flash_page_size_bytes = 0x800UL;

    CS_TEST_ASSERT_TRUE(cs_journal_load(&platform, &storage, &initial_state));

    fill_record(&record, 100U, 1U, 1U, 1);
    CS_TEST_ASSERT_TRUE(cs_journal_append(&platform, &storage, &initial_state, &record));
    fill_record(&record, 200U, 2U, 1U, 2);
    CS_TEST_ASSERT_TRUE(cs_journal_append(&platform, &storage, &initial_state, &record));

    meta_b_bytes = cs_flash_sim_address_ptr(&flash, storage.meta_b_address);
    CS_TEST_ASSERT_TRUE(meta_b_bytes != NULL);
    meta_b_bytes[0] ^= 0xFFU;

    CS_TEST_ASSERT_TRUE(cs_journal_load(&platform, &storage, &recovered_state));
    CS_TEST_ASSERT_EQ_U32(recovered_state.meta.record_count, 1U);

    cs_flash_sim_deinit(&flash);
    return EXIT_SUCCESS;
}

int main(void) {
    int rc;

    rc = test_journal_wrap_and_latest();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    rc = test_journal_recovers_from_corrupt_newer_meta_page();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    return EXIT_SUCCESS;
}
