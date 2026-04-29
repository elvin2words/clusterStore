#include "cs_boot_control.h"
#include "cs_test.h"
#include "flash_sim.h"
#include <string.h>

static int test_dual_copy_selection_and_fallback(void) {
    cs_flash_sim_t flash;
    cs_platform_t platform;
    cs_bcb_storage_t storage;
    cs_boot_control_block_t block;
    cs_boot_control_block_t loaded;
    cs_bcb_source_t source;
    uint8_t selected_slot;

    CS_TEST_ASSERT_TRUE(cs_flash_sim_init(&flash, 0x08008000UL, 0x2000UL, 0x1000UL));
    cs_flash_sim_bind_platform(&flash, &platform);
    storage.primary_address = 0x08008000UL;
    storage.shadow_address = 0x08009000UL;
    storage.page_size_bytes = 0x1000UL;

    cs_bcb_init(&block, CS_BCB_SLOT_A, 0x00010000UL);
    source = CS_BCB_SOURCE_NONE;
    CS_TEST_ASSERT_TRUE(cs_bcb_store(&platform, &storage, &block, source, &source));

    CS_TEST_ASSERT_TRUE(cs_bcb_read(&platform, &storage, &loaded, &source));
    CS_TEST_ASSERT_EQ_U32(loaded.active_slot, CS_BCB_SLOT_A);
    CS_TEST_ASSERT_EQ_U32(loaded.slot_state[CS_BCB_SLOT_A], CS_SLOT_CONFIRMED);

    CS_TEST_ASSERT_TRUE(cs_bcb_register_candidate(&loaded,
                                                  CS_BCB_SLOT_B,
                                                  0x00010100UL,
                                                  1U));
    CS_TEST_ASSERT_TRUE(cs_bcb_store(&platform, &storage, &loaded, source, &source));

    CS_TEST_ASSERT_TRUE(cs_bcb_read(&platform, &storage, &loaded, &source));
    CS_TEST_ASSERT_EQ_U32(loaded.active_slot, CS_BCB_SLOT_B);
    CS_TEST_ASSERT_EQ_U32(loaded.trial_boots_remaining, 1U);

    CS_TEST_ASSERT_EQ_U32(cs_bcb_select_slot(&loaded, true, &selected_slot),
                          CS_BOOT_DECISION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(selected_slot, CS_BCB_SLOT_B);
    CS_TEST_ASSERT_EQ_U32(loaded.trial_boots_remaining, 0U);

    CS_TEST_ASSERT_EQ_U32(cs_bcb_select_slot(&loaded, true, &selected_slot),
                          CS_BOOT_DECISION_BOOT_FALLBACK);
    CS_TEST_ASSERT_EQ_U32(selected_slot, CS_BCB_SLOT_A);
    CS_TEST_ASSERT_EQ_U32(loaded.active_slot, CS_BCB_SLOT_A);
    CS_TEST_ASSERT_EQ_U32(loaded.slot_state[CS_BCB_SLOT_B], CS_SLOT_BAD);

    cs_flash_sim_deinit(&flash);
    return EXIT_SUCCESS;
}

static int test_corrupt_newer_copy_recovers_from_older_copy(void) {
    cs_flash_sim_t flash;
    cs_platform_t platform;
    cs_bcb_storage_t storage;
    cs_boot_control_block_t block;
    cs_boot_control_block_t loaded;
    cs_bcb_source_t source;
    uint8_t *shadow_bytes;

    CS_TEST_ASSERT_TRUE(cs_flash_sim_init(&flash, 0x08008000UL, 0x2000UL, 0x1000UL));
    cs_flash_sim_bind_platform(&flash, &platform);
    storage.primary_address = 0x08008000UL;
    storage.shadow_address = 0x08009000UL;
    storage.page_size_bytes = 0x1000UL;

    cs_bcb_init(&block, CS_BCB_SLOT_A, 0x00010000UL);
    source = CS_BCB_SOURCE_NONE;
    CS_TEST_ASSERT_TRUE(cs_bcb_store(&platform, &storage, &block, source, &source));

    CS_TEST_ASSERT_TRUE(cs_bcb_register_candidate(&block,
                                                  CS_BCB_SLOT_B,
                                                  0x00010100UL,
                                                  3U));
    CS_TEST_ASSERT_TRUE(cs_bcb_store(&platform, &storage, &block, source, &source));
    shadow_bytes = cs_flash_sim_address_ptr(&flash, storage.shadow_address);
    CS_TEST_ASSERT_TRUE(shadow_bytes != NULL);
    shadow_bytes[0] ^= 0x5AU;

    CS_TEST_ASSERT_TRUE(cs_bcb_read(&platform, &storage, &loaded, &source));
    CS_TEST_ASSERT_EQ_U32(source, CS_BCB_SOURCE_PRIMARY);
    CS_TEST_ASSERT_EQ_U32(loaded.active_slot, CS_BCB_SLOT_A);

    cs_flash_sim_deinit(&flash);
    return EXIT_SUCCESS;
}

int main(void) {
    int rc;

    rc = test_dual_copy_selection_and_fallback();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    rc = test_corrupt_newer_copy_recovers_from_older_copy();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    return EXIT_SUCCESS;
}
