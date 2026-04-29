#ifndef CS_CAN_BENCH_NODE_H
#define CS_CAN_BENCH_NODE_H

#include <stdint.h>
#include "cs_bsp_g474.h"

#define CS_CAN_BENCH_HEARTBEAT_BASE_ID 0x650U
#define CS_CAN_BENCH_COMMAND_BASE_ID 0x750U
#define CS_CAN_BENCH_ACK_BASE_ID 0x760U

typedef struct {
    uint8_t node_id;
    uint32_t heartbeat_period_ms;
    uint32_t last_heartbeat_ms;
} cs_can_bench_node_t;

void cs_can_bench_node_init(cs_can_bench_node_t *node, uint8_t node_id);
cs_status_t cs_can_bench_node_step(cs_can_bench_node_t *node, cs_bsp_g474_t *bsp);

#endif
