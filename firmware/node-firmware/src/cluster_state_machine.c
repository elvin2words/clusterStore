#include "cluster_state_machine.h"

static bool supervision_lost(const cluster_state_input_t *input) {
    return !input->cluster_command_seen || !input->cluster_command_fresh;
}

cluster_state_t cluster_state_machine_step(cluster_state_t current_state,
                                           const cluster_state_input_t *input) {
    (void)current_state;

    if (input->local_fault_active || input->welded_contactor_detected) {
        return NODE_STATE_FAULT_LATCHED;
    }

    if (input->service_lockout) {
        return NODE_STATE_SERVICE_LOCKOUT;
    }

    if (input->maintenance_lockout || !input->contactor_feedback_ok) {
        return NODE_STATE_CLUSTER_ISOLATED;
    }

    if (input->cluster_command_rejected || supervision_lost(input)) {
        return NODE_STATE_STANDALONE_SAFE;
    }

    switch (input->requested_mode) {
        case NODE_MODE_CLUSTER_SLAVE_CHARGE:
            return NODE_STATE_CLUSTER_SLAVE_CHARGE;
        case NODE_MODE_CLUSTER_SLAVE_DISCHARGE:
            return NODE_STATE_CLUSTER_SLAVE_DISCHARGE;
        case NODE_MODE_CLUSTER_BALANCING:
            return input->allow_balancing
                       ? NODE_STATE_CLUSTER_BALANCING
                       : NODE_STATE_CLUSTER_STANDBY;
        case NODE_MODE_CLUSTER_ISOLATED:
            return NODE_STATE_CLUSTER_ISOLATED;
        case NODE_MODE_STANDBY:
        case NODE_MODE_IDLE:
        case NODE_MODE_CHARGE:
        case NODE_MODE_DISCHARGE:
        default:
            return NODE_STATE_CLUSTER_STANDBY;
    }
}

bool cluster_state_requires_open_contactors(cluster_state_t state) {
    return state == NODE_STATE_STANDALONE_SAFE ||
           state == NODE_STATE_CLUSTER_ISOLATED ||
           state == NODE_STATE_FAULT_LATCHED ||
           state == NODE_STATE_SERVICE_LOCKOUT;
}

bool cluster_state_allows_current_flow(cluster_state_t state) {
    return state == NODE_STATE_CLUSTER_SLAVE_CHARGE ||
           state == NODE_STATE_CLUSTER_SLAVE_DISCHARGE ||
           state == NODE_STATE_CLUSTER_BALANCING;
}
