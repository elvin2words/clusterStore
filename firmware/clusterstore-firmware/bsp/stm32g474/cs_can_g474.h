#ifndef CS_CAN_G474_H
#define CS_CAN_G474_H

#include <stdbool.h>
#include <stdint.h>
#include "cs_cluster_platform.h"

#define CS_G474_CAN_RX_RING_CAPACITY 32U
#define CS_G474_FDCAN_DEFAULT_KERNEL_CLOCK_HZ 80000000UL
#define CS_G474_FDCAN_DEFAULT_NOMINAL_BITRATE 500000UL

typedef struct {
    void *hfdcan;
    uint32_t kernel_clock_hz;
    uint32_t nominal_bitrate;
    uint8_t enable_rx_fifo0_irq;
} cs_g474_can_config_t;

typedef struct {
    void *hfdcan;
    uint32_t kernel_clock_hz;
    uint32_t nominal_bitrate;
    uint8_t enable_rx_fifo0_irq;
    cs_can_frame_t rx_ring[CS_G474_CAN_RX_RING_CAPACITY];
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
} cs_g474_can_t;

cs_status_t cs_can_g474_init(cs_g474_can_t *can_bus,
                             const cs_g474_can_config_t *config);
void cs_can_g474_push_rx_frame(cs_g474_can_t *can_bus, const cs_can_frame_t *frame);
cs_status_t cs_can_g474_start(cs_g474_can_t *can_bus);
cs_status_t cs_can_g474_send(void *context, const cs_can_frame_t *frame);
bool cs_can_g474_receive(void *context, cs_can_frame_t *frame);
cs_status_t cs_can_g474_drain_rx_fifo0(cs_g474_can_t *can_bus);

#endif
