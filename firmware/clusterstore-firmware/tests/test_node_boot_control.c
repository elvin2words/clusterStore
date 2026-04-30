#include "cluster_boot_control.h"
#include "cluster_flash_layout.h"
#include "cs_test.h"
#include <string.h>

#define MAKE_LAYOUT(cfg_ptr, layout_ptr)                                  \
    memset((cfg_ptr), 0, sizeof(*(cfg_ptr)));                             \
    (cfg_ptr)->flash_base_address = 0x08000000UL;                        \
    (cfg_ptr)->flash_size_bytes = 0x20000UL;                             \
    (cfg_ptr)->bootloader_size_bytes = 0x1000UL;                         \
    (cfg_ptr)->metadata_size_bytes = 0x1000UL;                           \
    (cfg_ptr)->journal_size_bytes = 0x1000UL;                            \
    (cfg_ptr)->slot_size_bytes = 0x8800UL;                               \
    CS_TEST_ASSERT_TRUE(cluster_flash_layout_build((cfg_ptr), (layout_ptr)))

static int test_init_and_validate(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_validate(&ctrl));
    CS_TEST_ASSERT_EQ_U32(ctrl.magic, CLUSTER_BOOT_CONTROL_MAGIC);
    CS_TEST_ASSERT_EQ_U32(ctrl.active_slot_id, CLUSTER_BOOT_SLOT_A);
    return EXIT_SUCCESS;
}

static int test_activate_slot_and_boot_active(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;
    cluster_boot_action_t action;
    uint8_t slot_id;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &ctrl, &layout, CLUSTER_BOOT_SLOT_B, "0.2.0",
        0x4000UL, 0xCAFEBABEUL, 3U));

    CS_TEST_ASSERT_EQ_U32(ctrl.active_slot_id, CLUSTER_BOOT_SLOT_B);
    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_B);
    return EXIT_SUCCESS;
}

static int test_confirm_prevents_fallback(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;
    cluster_boot_action_t action;
    uint8_t slot_id;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &ctrl, &layout, CLUSTER_BOOT_SLOT_B, "0.2.0",
        0x4000UL, 0xCAFEBABEUL, 1U));

    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_B);
    CS_TEST_ASSERT_TRUE(cluster_boot_control_confirm_active(&ctrl));

    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_B);
    return EXIT_SUCCESS;
}

static int test_trial_exhaustion_falls_back(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;
    cluster_boot_action_t action;
    uint8_t slot_id;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &ctrl, &layout, CLUSTER_BOOT_SLOT_B, "0.2.0",
        0x4000UL, 0xCAFEBABEUL, 1U));

    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_B);

    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_FALLBACK);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_A);
    return EXIT_SUCCESS;
}

static int test_rollback_returns_to_fallback(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;
    cluster_boot_action_t action;
    uint8_t slot_id;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &ctrl, &layout, CLUSTER_BOOT_SLOT_B, "0.2.0",
        0x4000UL, 0xCAFEBABEUL, 3U));
    CS_TEST_ASSERT_TRUE(cluster_boot_control_confirm_active(&ctrl));
    CS_TEST_ASSERT_TRUE(cluster_boot_control_request_rollback(&ctrl));

    action = cluster_boot_control_select_boot_slot(&ctrl, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_FALLBACK);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_A);
    return EXIT_SUCCESS;
}

static int test_crc_validation(void) {
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_boot_control_block_t ctrl;

    MAKE_LAYOUT(&cfg, &layout);
    cluster_boot_control_init(&ctrl, &layout, CLUSTER_BOOT_SLOT_A, "0.1.0");
    CS_TEST_ASSERT_TRUE(cluster_boot_control_validate(&ctrl));

    ctrl.magic ^= 0x1U;
    CS_TEST_ASSERT_FALSE(cluster_boot_control_validate(&ctrl));
    return EXIT_SUCCESS;
}

int main(void) {
    int result = EXIT_SUCCESS;
    if (test_init_and_validate() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_activate_slot_and_boot_active() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_confirm_prevents_fallback() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_trial_exhaustion_falls_back() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_rollback_returns_to_fallback() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_crc_validation() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    return result;
}
