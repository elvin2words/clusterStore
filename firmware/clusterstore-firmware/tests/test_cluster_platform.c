#include "cs_cluster_platform.h"
#include "cs_test.h"
#include "flash_sim.h"
#include <stdint.h>

static int test_platform_flash_wrappers_and_erase_rule(void) {
    cs_flash_sim_t flash;
    cs_platform_t platform;
    uint32_t address = 0x0800A000UL;
    uint8_t payload = 0x11U;
    uint8_t readback = 0U;

    CS_TEST_ASSERT_TRUE(cs_flash_sim_init(&flash, address, 0x800UL, 0x800UL));
    cs_flash_sim_bind_platform(&flash, &platform);

    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_write(&platform,
                                                  address,
                                                  &payload,
                                                  1U),
                          CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_read(&platform,
                                                 address,
                                                 &readback,
                                                 1U),
                          CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(readback, payload);

    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_write(&platform,
                                                  address,
                                                  &payload,
                                                  1U),
                          CS_STATUS_ERROR);

    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_erase(&platform, address, 0x800UL),
                          CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_write(&platform,
                                                  address,
                                                  &payload,
                                                  1U),
                          CS_STATUS_OK);

    cs_flash_sim_deinit(&flash);
    return EXIT_SUCCESS;
}

static int test_unbound_platform_paths_fail_cleanly(void) {
    cs_platform_t platform;
    uint8_t byte = 0U;
    cs_can_frame_t frame;

    cs_platform_init(&platform);
    frame.id = 0U;
    frame.dlc = 0U;

    CS_TEST_ASSERT_EQ_U32(cs_platform_flash_read(&platform, 0U, &byte, 1U),
                          CS_STATUS_INVALID_ARGUMENT);
    CS_TEST_ASSERT_FALSE(cs_platform_can_receive(&platform, &frame));
    CS_TEST_ASSERT_EQ_U32(cs_platform_monotonic_ms(&platform), 0U);
    return EXIT_SUCCESS;
}

int main(void) {
    int rc;

    rc = test_platform_flash_wrappers_and_erase_rule();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    rc = test_unbound_platform_paths_fail_cleanly();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    return EXIT_SUCCESS;
}
