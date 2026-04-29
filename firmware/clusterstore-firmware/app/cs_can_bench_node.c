#include "cs_can_bench_node.h"
#include <stddef.h>
#include <string.h>

static cs_status_t cs_can_bench_send_heartbeat(cs_can_bench_node_t *node,
                                               cs_bsp_g474_t *bsp,
                                               const cs_bsp_measurements_t *measurements) {
    cs_can_frame_t frame;
    int16_t current_da;
    int16_t pack_dv;
    int16_t bus_dv;
    int16_t temperature_dc;

    memset(&frame, 0, sizeof(frame));
    current_da = (int16_t)(measurements->current_ma / 100);
    pack_dv = (int16_t)(measurements->pack_voltage_mv / 10U);
    bus_dv = (int16_t)(measurements->bus_voltage_mv / 10U);
    temperature_dc = (int16_t)measurements->temperature_deci_c;

    frame.id = CS_CAN_BENCH_HEARTBEAT_BASE_ID + node->node_id;
    frame.dlc = 8U;
    frame.data[0] = (uint8_t)(pack_dv & 0xFF);
    frame.data[1] = (uint8_t)((pack_dv >> 8U) & 0xFF);
    frame.data[2] = (uint8_t)(bus_dv & 0xFF);
    frame.data[3] = (uint8_t)((bus_dv >> 8U) & 0xFF);
    frame.data[4] = (uint8_t)(current_da & 0xFF);
    frame.data[5] = (uint8_t)((current_da >> 8U) & 0xFF);
    frame.data[6] = (uint8_t)(temperature_dc / 10);
    frame.data[7] = (measurements->current_valid ? 0x01U : 0U) |
                    (measurements->voltages_valid ? 0x02U : 0U) |
                    (measurements->temperature_valid ? 0x04U : 0U);
    return cs_platform_can_send(&bsp->platform, &frame);
}

static cs_status_t cs_can_bench_send_ack(cs_can_bench_node_t *node,
                                         cs_bsp_g474_t *bsp,
                                         uint8_t opcode,
                                         uint8_t sequence) {
    cs_can_frame_t frame;
    uint32_t uptime_s;

    memset(&frame, 0, sizeof(frame));
    uptime_s = cs_platform_monotonic_ms(&bsp->platform) / 1000U;
    frame.id = CS_CAN_BENCH_ACK_BASE_ID + node->node_id;
    frame.dlc = 8U;
    frame.data[0] = opcode;
    frame.data[1] = sequence;
    frame.data[2] = (uint8_t)(uptime_s & 0xFFU);
    frame.data[3] = (uint8_t)((uptime_s >> 8U) & 0xFFU);
    frame.data[4] = (uint8_t)((uptime_s >> 16U) & 0xFFU);
    frame.data[5] = (uint8_t)((uptime_s >> 24U) & 0xFFU);
    frame.data[6] = 0x43U;
    frame.data[7] = 0x53U;
    return cs_platform_can_send(&bsp->platform, &frame);
}

static cs_status_t cs_can_bench_handle_command(cs_can_bench_node_t *node,
                                               cs_bsp_g474_t *bsp,
                                               const cs_can_frame_t *frame) {
    cs_bsp_measurements_t measurements;

    if (frame == NULL || frame->dlc == 0U) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    switch (frame->data[0]) {
        case 0x01U:
            return cs_can_bench_send_ack(node, bsp, frame->data[0], frame->data[1]);
        case 0x02U:
            if (cs_bsp_g474_sample_measurements(bsp, &measurements) != CS_STATUS_OK) {
                return CS_STATUS_ERROR;
            }
            if (cs_can_bench_send_heartbeat(node, bsp, &measurements) != CS_STATUS_OK) {
                return CS_STATUS_ERROR;
            }
            return cs_can_bench_send_ack(node, bsp, frame->data[0], frame->data[1]);
        default:
            return cs_can_bench_send_ack(node, bsp, 0x7FU, frame->data[1]);
    }
}

void cs_can_bench_node_init(cs_can_bench_node_t *node, uint8_t node_id) {
    if (node == NULL) {
        return;
    }

    memset(node, 0, sizeof(*node));
    node->node_id = node_id;
    node->heartbeat_period_ms = 1000U;
}

cs_status_t cs_can_bench_node_step(cs_can_bench_node_t *node, cs_bsp_g474_t *bsp) {
    cs_can_frame_t frame;
    cs_bsp_measurements_t measurements;
    uint32_t now_ms;

    if (node == NULL || bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    (void)cs_bsp_g474_watchdog_kick(bsp);
    now_ms = cs_platform_monotonic_ms(&bsp->platform);

    while (cs_platform_can_receive(&bsp->platform, &frame)) {
        if (frame.id == (CS_CAN_BENCH_COMMAND_BASE_ID + node->node_id)) {
            cs_status_t status;

            status = cs_can_bench_handle_command(node, bsp, &frame);
            if (status != CS_STATUS_OK) {
                return status;
            }

            if (frame.data[0] == 0x02U) {
                node->last_heartbeat_ms = now_ms;
            }
        }
    }

    if ((now_ms - node->last_heartbeat_ms) >= node->heartbeat_period_ms) {
        if (cs_bsp_g474_sample_measurements(bsp, &measurements) == CS_STATUS_OK) {
            cs_status_t status;

            status = cs_can_bench_send_heartbeat(node, bsp, &measurements);
            if (status != CS_STATUS_OK) {
                return status;
            }
            node->last_heartbeat_ms = now_ms;
        }
    }

    return CS_STATUS_OK;
}
