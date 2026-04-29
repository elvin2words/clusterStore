#include "cs_adc_g474.h"
#include "cs_bsp_g474.h"
#include "cs_can_bench_node.h"
#include "cs_flash_g474.h"
#include "cs_ina228.h"
#include "cs_test.h"
#include <string.h>

typedef struct {
    cs_can_frame_t rx_queue[4];
    uint8_t rx_count;
    uint8_t rx_index;
    cs_can_frame_t tx_history[4];
    uint8_t tx_count;
    uint32_t now_ms;
    uint32_t watchdog_kicks;
    cs_bsp_measurements_t measurements;
} test_runtime_t;

static uint32_t test_now_ms(void *context) {
    test_runtime_t *runtime;

    runtime = (test_runtime_t *)context;
    return runtime->now_ms;
}

static cs_status_t test_can_send(void *context, const cs_can_frame_t *frame) {
    test_runtime_t *runtime;

    runtime = (test_runtime_t *)context;
    if (runtime == NULL || frame == NULL || runtime->tx_count >= 4U) {
        return CS_STATUS_ERROR;
    }

    runtime->tx_history[runtime->tx_count] = *frame;
    runtime->tx_count += 1U;
    return CS_STATUS_OK;
}

static bool test_can_receive(void *context, cs_can_frame_t *frame) {
    test_runtime_t *runtime;

    runtime = (test_runtime_t *)context;
    if (runtime == NULL || frame == NULL || runtime->rx_index >= runtime->rx_count) {
        return false;
    }

    *frame = runtime->rx_queue[runtime->rx_index];
    runtime->rx_index += 1U;
    return true;
}

static cs_status_t test_sample_measurements(void *context,
                                            cs_bsp_measurements_t *measurements) {
    test_runtime_t *runtime;

    runtime = (test_runtime_t *)context;
    if (runtime == NULL || measurements == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    *measurements = runtime->measurements;
    measurements->timestamp_ms = runtime->now_ms;
    return CS_STATUS_OK;
}

static cs_status_t test_watchdog_kick(void *context) {
    test_runtime_t *runtime;

    runtime = (test_runtime_t *)context;
    if (runtime == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    runtime->watchdog_kicks += 1U;
    return CS_STATUS_OK;
}

static int test_no_hal_bsp_init_and_bench_round_trip(void) {
    static uint8_t flash_bytes[0x2000];
    test_runtime_t runtime;
    cs_bsp_g474_t bsp;
    cs_bsp_g474_config_t config;
    cs_can_bench_node_t node;

    memset(flash_bytes, 0xFF, sizeof(flash_bytes));
    memset(&runtime, 0, sizeof(runtime));
    memset(&config, 0, sizeof(config));

    runtime.now_ms = 1500U;
    runtime.measurements.pack_voltage_mv = 52000U;
    runtime.measurements.bus_voltage_mv = 51500U;
    runtime.measurements.current_ma = -2400;
    runtime.measurements.temperature_deci_c = 273;
    runtime.measurements.current_valid = true;
    runtime.measurements.voltages_valid = true;
    runtime.measurements.temperature_valid = true;
    runtime.rx_queue[0].id = CS_CAN_BENCH_COMMAND_BASE_ID + 1U;
    runtime.rx_queue[0].dlc = 8U;
    runtime.rx_queue[0].data[0] = 0x02U;
    runtime.rx_queue[0].data[1] = 0x34U;
    runtime.rx_count = 1U;

    config.time_context = &runtime;
    config.now_ms = test_now_ms;
    config.measurement_context = &runtime;
    config.sample_measurements = test_sample_measurements;
    config.watchdog_context = &runtime;
    config.watchdog_kick = test_watchdog_kick;
    config.flash.flash_base_address = 0x08000000UL;
    config.flash.flash_size_bytes = sizeof(flash_bytes);
    config.flash.page_size_bytes = 0x800UL;
    config.flash.bank_size_bytes = 0x1000UL;
    config.flash.bank_count = 2U;
    config.flash.host_flash_bytes = flash_bytes;

    CS_TEST_ASSERT_EQ_U32(cs_bsp_g474_init(&bsp, &config), CS_STATUS_OK);

    bsp.platform.can.context = &runtime;
    bsp.platform.can.send = test_can_send;
    bsp.platform.can.receive = test_can_receive;

    cs_can_bench_node_init(&node, 1U);
    node.last_heartbeat_ms = 0U;

    CS_TEST_ASSERT_EQ_U32(cs_can_bench_node_step(&node, &bsp), CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(runtime.watchdog_kicks, 1U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_count, 2U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].id, CS_CAN_BENCH_HEARTBEAT_BASE_ID + 1U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[0], 80U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[2], 30U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[4], 232U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[5], 255U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[6], 27U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[0].data[7], 0x07U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[1].id, CS_CAN_BENCH_ACK_BASE_ID + 1U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[1].data[0], 0x02U);
    CS_TEST_ASSERT_EQ_U32(runtime.tx_history[1].data[1], 0x34U);
    return EXIT_SUCCESS;
}

static int test_adc_and_ina228_helpers(void) {
    uint32_t pack_mv;
    int32_t temperature_deci_c;
    int32_t current_ma;

    pack_mv = cs_adc_g474_scale_divider_mv_from_raw(2048U,
                                                    4096U,
                                                    3300U,
                                                    100000U,
                                                    10000U);
    CS_TEST_ASSERT_EQ_U32(pack_mv, 18150U);

    temperature_deci_c = cs_adc_g474_ntc_temperature_deci_c_from_raw(2048U,
                                                                      4096U,
                                                                      10000U,
                                                                      10000U,
                                                                      250,
                                                                      3950U);
    CS_TEST_ASSERT(temperature_deci_c >= 249 && temperature_deci_c <= 251);

    current_ma = cs_ina228_current_ma_from_raw(256, 2500U);
    CS_TEST_ASSERT_EQ_U32(current_ma, 640);
    current_ma = cs_ina228_current_ma_from_raw(-256, 2500U);
    CS_TEST_ASSERT_EQ_U32((uint32_t)current_ma, (uint32_t)-640);
    return EXIT_SUCCESS;
}

static int test_flash_alignment_and_bank_crossing(void) {
    static uint8_t flash_bytes[0x2000];
    cs_g474_flash_t flash;
    cs_g474_flash_config_t config;
    uint8_t bank_index;
    uint32_t page_index;
    uint64_t payload = 0x1122334455667788ULL;

    memset(flash_bytes, 0x00, sizeof(flash_bytes));
    memset(&config, 0, sizeof(config));
    config.flash_base_address = 0x08000000UL;
    config.flash_size_bytes = sizeof(flash_bytes);
    config.page_size_bytes = 0x800UL;
    config.bank_size_bytes = 0x1000UL;
    config.bank_count = 2U;
    config.host_flash_bytes = flash_bytes;

    CS_TEST_ASSERT_EQ_U32(cs_flash_g474_init(&flash, &config), CS_STATUS_OK);
    CS_TEST_ASSERT_TRUE(cs_flash_g474_address_to_bank_page(&flash,
                                                           0x08000800UL,
                                                           &bank_index,
                                                           &page_index));
    CS_TEST_ASSERT_EQ_U32(bank_index, 0U);
    CS_TEST_ASSERT_EQ_U32(page_index, 1U);
    CS_TEST_ASSERT_TRUE(cs_flash_g474_address_to_bank_page(&flash,
                                                           0x08001000UL,
                                                           &bank_index,
                                                           &page_index));
    CS_TEST_ASSERT_EQ_U32(bank_index, 1U);
    CS_TEST_ASSERT_EQ_U32(page_index, 0U);

    CS_TEST_ASSERT_EQ_U32(cs_flash_g474_erase(&flash, 0x08000800UL, 0x1000UL),
                          CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(cs_flash_g474_write(&flash,
                                              0x08000801UL,
                                              &payload,
                                              sizeof(payload)),
                          CS_STATUS_INVALID_ARGUMENT);
    CS_TEST_ASSERT_EQ_U32(cs_flash_g474_write(&flash,
                                              0x08000800UL,
                                              &payload,
                                              sizeof(payload)),
                          CS_STATUS_OK);
    CS_TEST_ASSERT_EQ_U32(cs_flash_g474_write(&flash,
                                              0x08000800UL,
                                              &payload,
                                              sizeof(payload)),
                          CS_STATUS_ERROR);
    return EXIT_SUCCESS;
}

int main(void) {
    int rc;

    rc = test_no_hal_bsp_init_and_bench_round_trip();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    rc = test_adc_and_ina228_helpers();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    rc = test_flash_alignment_and_bank_crossing();
    if (rc != EXIT_SUCCESS) {
        return rc;
    }

    return EXIT_SUCCESS;
}
