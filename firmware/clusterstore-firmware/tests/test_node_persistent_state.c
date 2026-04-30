#include "cluster_persistent_state.h"
#include "cluster_bootloader_runtime.h"
#include "cluster_boot_control.h"
#include "cluster_flash_layout.h"
#include "cs_test.h"
#include "cluster_platform_sim.h"
#include <string.h>
#include <stdlib.h>

#define TEST_FLASH_BASE    0x08000000UL
#define TEST_FLASH_SIZE    0x20000UL
#define TEST_PAGE_SIZE     0x800UL
#define TEST_BOOT_SIZE     0x1000UL
#define TEST_META_SIZE     0x1000UL
#define TEST_JOURNAL_SIZE  0x1000UL
#define TEST_SLOT_SIZE     0x8800UL
#define TEST_JOURNAL_CAP   4U

static void fill_layout_config(cluster_flash_layout_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->flash_base_address = TEST_FLASH_BASE;
    cfg->flash_size_bytes = TEST_FLASH_SIZE;
    cfg->bootloader_size_bytes = TEST_BOOT_SIZE;
    cfg->metadata_size_bytes = TEST_META_SIZE;
    cfg->journal_size_bytes = TEST_JOURNAL_SIZE;
    cfg->slot_size_bytes = TEST_SLOT_SIZE;
}

static int test_fresh_load_uses_defaults(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_persistent_state_t state;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);
    CS_TEST_ASSERT_TRUE(cluster_flash_layout_build(&cfg, &layout));

    cluster_persistent_state_init(&state, &platform, &layout, TEST_JOURNAL_CAP);
    CS_TEST_ASSERT_TRUE(
        cluster_persistent_state_load(&state, CLUSTER_BOOT_SLOT_A, "0.1.0"));
    CS_TEST_ASSERT_EQ_U32(state.boot_control.active_slot_id, CLUSTER_BOOT_SLOT_A);
    CS_TEST_ASSERT_EQ_U32(state.generation, 0U);
    CS_TEST_ASSERT_EQ_U32(state.journal_capacity, TEST_JOURNAL_CAP);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

static int test_save_and_reload_round_trip(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_persistent_state_t state;
    cluster_persistent_state_t reloaded;
    uint32_t saved_generation;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);
    CS_TEST_ASSERT_TRUE(cluster_flash_layout_build(&cfg, &layout));

    cluster_persistent_state_init(&state, &platform, &layout, TEST_JOURNAL_CAP);
    CS_TEST_ASSERT_TRUE(
        cluster_persistent_state_load(&state, CLUSTER_BOOT_SLOT_A, "0.1.0"));
    CS_TEST_ASSERT_TRUE(cluster_persistent_state_save(&state));
    saved_generation = state.generation;
    CS_TEST_ASSERT_TRUE(saved_generation > 0U);

    cluster_persistent_state_init(&reloaded, &platform, &layout, TEST_JOURNAL_CAP);
    CS_TEST_ASSERT_TRUE(
        cluster_persistent_state_load(&reloaded, CLUSTER_BOOT_SLOT_A, "0.1.0"));
    CS_TEST_ASSERT_EQ_U32(reloaded.generation, saved_generation);
    CS_TEST_ASSERT_EQ_U32(reloaded.boot_control.active_slot_id, CLUSTER_BOOT_SLOT_A);
    CS_TEST_ASSERT_EQ_U32(reloaded.journal_capacity, TEST_JOURNAL_CAP);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

static int test_generation_advances_on_each_save(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_flash_layout_t layout;
    cluster_persistent_state_t state;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);
    CS_TEST_ASSERT_TRUE(cluster_flash_layout_build(&cfg, &layout));

    cluster_persistent_state_init(&state, &platform, &layout, TEST_JOURNAL_CAP);
    CS_TEST_ASSERT_TRUE(
        cluster_persistent_state_load(&state, CLUSTER_BOOT_SLOT_A, "0.1.0"));
    CS_TEST_ASSERT_EQ_U32(state.generation, 0U);

    CS_TEST_ASSERT_TRUE(cluster_persistent_state_save(&state));
    CS_TEST_ASSERT_EQ_U32(state.generation, 1U);

    CS_TEST_ASSERT_TRUE(cluster_persistent_state_save(&state));
    CS_TEST_ASSERT_EQ_U32(state.generation, 2U);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

static int test_bootloader_runtime_fresh_flash_stays_in_bootloader(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_bootloader_runtime_t runtime;
    cluster_bootloader_runtime_config_t boot_cfg;
    cluster_boot_action_t action;
    uint8_t slot_id;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);

    memset(&boot_cfg, 0, sizeof(boot_cfg));
    boot_cfg.platform = &platform;
    boot_cfg.flash_layout = cfg;
    boot_cfg.default_active_slot_id = CLUSTER_BOOT_SLOT_A;
    boot_cfg.default_version = "0.1.0";
    boot_cfg.verify_crc32_before_boot = false;
    boot_cfg.verify_image = NULL;
    boot_cfg.jump_to_image = NULL;

    CS_TEST_ASSERT_TRUE(cluster_bootloader_runtime_init(&runtime, &boot_cfg));
    action = cluster_bootloader_runtime_select(&runtime, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_STAY_IN_BOOTLOADER);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

static int test_bootloader_runtime_boots_activated_slot(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_bootloader_runtime_t runtime;
    cluster_bootloader_runtime_config_t boot_cfg;
    cluster_boot_action_t action;
    uint8_t slot_id;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);

    memset(&boot_cfg, 0, sizeof(boot_cfg));
    boot_cfg.platform = &platform;
    boot_cfg.flash_layout = cfg;
    boot_cfg.default_active_slot_id = CLUSTER_BOOT_SLOT_A;
    boot_cfg.default_version = "0.1.0";
    boot_cfg.verify_crc32_before_boot = false;
    boot_cfg.verify_image = NULL;
    boot_cfg.jump_to_image = NULL;

    CS_TEST_ASSERT_TRUE(cluster_bootloader_runtime_init(&runtime, &boot_cfg));

    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &runtime.persistent_state.boot_control,
        &runtime.flash_layout,
        CLUSTER_BOOT_SLOT_A,
        "0.1.0",
        0x4000UL,
        0xDEADBEEFUL,
        3U));
    cluster_boot_control_update_crc(&runtime.persistent_state.boot_control);
    CS_TEST_ASSERT_TRUE(cluster_persistent_state_save(&runtime.persistent_state));

    action = cluster_bootloader_runtime_select(&runtime, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_A);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

static int test_bootloader_runtime_trial_exhaustion_fallback(void) {
    cluster_platform_sim_t sim;
    cluster_platform_t platform;
    cluster_flash_layout_config_t cfg;
    cluster_bootloader_runtime_t runtime;
    cluster_bootloader_runtime_config_t boot_cfg;
    cluster_boot_action_t action;
    uint8_t slot_id;

    fill_layout_config(&cfg);
    CS_TEST_ASSERT_TRUE(cluster_platform_sim_init(
        &sim, TEST_FLASH_BASE, TEST_FLASH_SIZE, TEST_PAGE_SIZE));
    cluster_platform_sim_bind(&sim, &platform);

    memset(&boot_cfg, 0, sizeof(boot_cfg));
    boot_cfg.platform = &platform;
    boot_cfg.flash_layout = cfg;
    boot_cfg.default_active_slot_id = CLUSTER_BOOT_SLOT_A;
    boot_cfg.default_version = "0.1.0";
    boot_cfg.verify_crc32_before_boot = false;
    boot_cfg.verify_image = NULL;
    boot_cfg.jump_to_image = NULL;

    CS_TEST_ASSERT_TRUE(cluster_bootloader_runtime_init(&runtime, &boot_cfg));

    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &runtime.persistent_state.boot_control,
        &runtime.flash_layout,
        CLUSTER_BOOT_SLOT_A,
        "0.1.0",
        0x4000UL,
        0xDEADBEEFUL,
        1U));
    cluster_boot_control_update_crc(&runtime.persistent_state.boot_control);

    CS_TEST_ASSERT_TRUE(cluster_boot_control_activate_slot(
        &runtime.persistent_state.boot_control,
        &runtime.flash_layout,
        CLUSTER_BOOT_SLOT_B,
        "0.1.0-fallback",
        0x4000UL,
        0xCAFEBABEUL,
        0U));
    cluster_boot_control_update_crc(&runtime.persistent_state.boot_control);
    CS_TEST_ASSERT_TRUE(cluster_persistent_state_save(&runtime.persistent_state));

    action = cluster_bootloader_runtime_select(&runtime, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_ACTIVE);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_A);

    action = cluster_bootloader_runtime_select(&runtime, &slot_id);
    CS_TEST_ASSERT_EQ_U32(action, CLUSTER_BOOT_ACTION_BOOT_FALLBACK);
    CS_TEST_ASSERT_EQ_U32(slot_id, CLUSTER_BOOT_SLOT_B);

    cluster_platform_sim_deinit(&sim);
    return EXIT_SUCCESS;
}

int main(void) {
    int result = EXIT_SUCCESS;
    if (test_fresh_load_uses_defaults() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_save_and_reload_round_trip() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_generation_advances_on_each_save() != EXIT_SUCCESS) { result = EXIT_FAILURE; }
    if (test_bootloader_runtime_fresh_flash_stays_in_bootloader() != EXIT_SUCCESS) {
        result = EXIT_FAILURE;
    }
    if (test_bootloader_runtime_boots_activated_slot() != EXIT_SUCCESS) {
        result = EXIT_FAILURE;
    }
    if (test_bootloader_runtime_trial_exhaustion_fallback() != EXIT_SUCCESS) {
        result = EXIT_FAILURE;
    }
    return result;
}
