#ifndef CLUSTER_STATE_MACHINE_H
#define CLUSTER_STATE_MACHINE_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_can_protocol.h"

typedef enum {
    NODE_STATE_STANDALONE_SAFE = 0,
    NODE_STATE_CLUSTER_STANDBY = 1,
    NODE_STATE_CLUSTER_SLAVE_CHARGE = 2,
    NODE_STATE_CLUSTER_SLAVE_DISCHARGE = 3,
    NODE_STATE_CLUSTER_BALANCING = 4,
    NODE_STATE_CLUSTER_ISOLATED = 5,
    NODE_STATE_FAULT_LATCHED = 6,
    NODE_STATE_SERVICE_LOCKOUT = 7
} cluster_state_t;

typedef struct {
    bool cluster_command_seen;
    bool cluster_command_fresh;
    bool cluster_command_rejected;
    bool local_fault_active;
    bool maintenance_lockout;
    bool service_lockout;
    bool contactor_feedback_ok;
    bool welded_contactor_detected;
    bool allow_balancing;
    cluster_node_mode_t requested_mode;
} cluster_state_input_t;

cluster_state_t cluster_state_machine_step(cluster_state_t current_state,
                                           const cluster_state_input_t *input);
bool cluster_state_requires_open_contactors(cluster_state_t state);
bool cluster_state_allows_current_flow(cluster_state_t state);

#endif
