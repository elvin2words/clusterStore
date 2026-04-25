#ifndef CLUSTER_COMMAND_MANAGER_H
#define CLUSTER_COMMAND_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_can_protocol.h"

typedef enum {
    CLUSTER_COMMAND_REJECT_NONE = 0,
    CLUSTER_COMMAND_REJECT_STALE_SEQUENCE = 1,
    CLUSTER_COMMAND_REJECT_TIMEOUT_INVALID = 2,
    CLUSTER_COMMAND_REJECT_TIMEOUT = 3,
    CLUSTER_COMMAND_REJECT_LOCAL_FAULT = 4,
    CLUSTER_COMMAND_REJECT_MAINTENANCE_LOCKOUT = 5,
    CLUSTER_COMMAND_REJECT_SERVICE_LOCKOUT = 6,
    CLUSTER_COMMAND_REJECT_CURRENT_LIMIT = 7,
    CLUSTER_COMMAND_REJECT_MODE_POLICY = 8,
    CLUSTER_COMMAND_REJECT_INVALID_FLAGS = 9
} cluster_command_rejection_reason_t;

typedef struct {
    bool local_fault_active;
    bool maintenance_lockout;
    bool service_lockout;
    bool balancing_supported;
    int16_t max_charge_setpoint_da;
    int16_t max_discharge_setpoint_da;
    uint16_t max_supervision_timeout_ms;
} cluster_command_policy_t;

typedef struct {
    bool valid;
    bool fresh;
    bool seen_once;
    uint32_t last_rx_time_ms;
    uint16_t effective_timeout_ms;
    uint8_t last_sequence;
    node_command_payload_t last_command;
    cluster_command_rejection_reason_t last_rejection;
    uint32_t accepted_counter;
    uint32_t rejected_counter;
    uint32_t stale_counter;
    uint32_t timeout_counter;
} cluster_command_tracker_t;

void cluster_command_tracker_init(cluster_command_tracker_t *tracker,
                                  uint16_t default_timeout_ms);
bool cluster_command_tracker_receive(cluster_command_tracker_t *tracker,
                                     const node_command_payload_t *command,
                                     const cluster_command_policy_t *policy,
                                     uint32_t now_ms);
bool cluster_command_tracker_tick(cluster_command_tracker_t *tracker,
                                  uint32_t now_ms);
uint32_t cluster_command_tracker_age_ms(const cluster_command_tracker_t *tracker,
                                        uint32_t now_ms);
bool cluster_command_tracker_is_fresh(const cluster_command_tracker_t *tracker);

#endif

