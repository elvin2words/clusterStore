#include "cluster_command_manager.h"

static uint32_t cluster_elapsed_ms(uint32_t now_ms, uint32_t then_ms) {
    return now_ms >= then_ms
               ? (now_ms - then_ms)
               : (UINT32_MAX - then_ms + now_ms + 1U);
}

static bool cluster_command_requests_current(const node_command_payload_t *command) {
    return command->charge_setpoint_da > 0 || command->discharge_setpoint_da > 0;
}

static cluster_command_rejection_reason_t cluster_validate_command(
    const cluster_command_tracker_t *tracker,
    const node_command_payload_t *command,
    const cluster_command_policy_t *policy) {
    uint16_t requested_timeout_ms;

    if (tracker->seen_once && tracker->last_sequence == command->command_sequence) {
        return CLUSTER_COMMAND_REJECT_STALE_SEQUENCE;
    }

    requested_timeout_ms = (uint16_t)command->supervision_timeout_100ms * 100U;
    if (requested_timeout_ms == 0U ||
        requested_timeout_ms > policy->max_supervision_timeout_ms) {
        return CLUSTER_COMMAND_REJECT_TIMEOUT_INVALID;
    }

    if (command->charge_setpoint_da < 0 ||
        command->charge_setpoint_da > policy->max_charge_setpoint_da ||
        command->discharge_setpoint_da < 0 ||
        command->discharge_setpoint_da > policy->max_discharge_setpoint_da) {
        return CLUSTER_COMMAND_REJECT_CURRENT_LIMIT;
    }

    if (command->charge_setpoint_da > 0 && command->discharge_setpoint_da > 0) {
        return CLUSTER_COMMAND_REJECT_INVALID_FLAGS;
    }

    if (command->mode == NODE_MODE_CLUSTER_BALANCING &&
        (((command->command_flags & NODE_COMMAND_FLAG_ALLOW_BALANCING) == 0U) ||
         !policy->balancing_supported)) {
        return CLUSTER_COMMAND_REJECT_MODE_POLICY;
    }

    if (command->mode == NODE_MODE_CLUSTER_ISOLATED &&
        (((command->command_flags & NODE_COMMAND_FLAG_CONTACTOR_CLOSED) != 0U) ||
         cluster_command_requests_current(command))) {
        return CLUSTER_COMMAND_REJECT_INVALID_FLAGS;
    }

    if (policy->local_fault_active &&
        (((command->command_flags & NODE_COMMAND_FLAG_CONTACTOR_CLOSED) != 0U) ||
         cluster_command_requests_current(command))) {
        return CLUSTER_COMMAND_REJECT_LOCAL_FAULT;
    }

    if (policy->maintenance_lockout &&
        (((command->command_flags & NODE_COMMAND_FLAG_CONTACTOR_CLOSED) != 0U) ||
         cluster_command_requests_current(command))) {
        return CLUSTER_COMMAND_REJECT_MAINTENANCE_LOCKOUT;
    }

    if (policy->service_lockout &&
        (((command->command_flags & NODE_COMMAND_FLAG_CONTACTOR_CLOSED) != 0U) ||
         cluster_command_requests_current(command))) {
        return CLUSTER_COMMAND_REJECT_SERVICE_LOCKOUT;
    }

    if (((command->command_flags & NODE_COMMAND_FLAG_ALLOW_BALANCING) != 0U) &&
        command->mode != NODE_MODE_CLUSTER_BALANCING &&
        command->mode != NODE_MODE_CLUSTER_SLAVE_CHARGE &&
        command->mode != NODE_MODE_STANDBY) {
        return CLUSTER_COMMAND_REJECT_INVALID_FLAGS;
    }

    return CLUSTER_COMMAND_REJECT_NONE;
}

void cluster_command_tracker_init(cluster_command_tracker_t *tracker,
                                  uint16_t default_timeout_ms) {
    tracker->valid = false;
    tracker->fresh = false;
    tracker->seen_once = false;
    tracker->last_rx_time_ms = 0U;
    tracker->effective_timeout_ms = default_timeout_ms;
    tracker->last_sequence = 0U;
    tracker->last_command.mode = NODE_MODE_CLUSTER_ISOLATED;
    tracker->last_command.charge_setpoint_da = 0;
    tracker->last_command.discharge_setpoint_da = 0;
    tracker->last_command.command_flags = 0U;
    tracker->last_command.supervision_timeout_100ms =
        (uint8_t)(default_timeout_ms / 100U);
    tracker->last_command.command_sequence = 0U;
    tracker->last_rejection = CLUSTER_COMMAND_REJECT_NONE;
    tracker->accepted_counter = 0U;
    tracker->rejected_counter = 0U;
    tracker->stale_counter = 0U;
    tracker->timeout_counter = 0U;
}

bool cluster_command_tracker_receive(cluster_command_tracker_t *tracker,
                                     const node_command_payload_t *command,
                                     const cluster_command_policy_t *policy,
                                     uint32_t now_ms) {
    cluster_command_rejection_reason_t rejection;

    rejection = cluster_validate_command(tracker, command, policy);
    tracker->last_rejection = rejection;
    if (rejection != CLUSTER_COMMAND_REJECT_NONE) {
        tracker->rejected_counter += 1U;
        if (rejection == CLUSTER_COMMAND_REJECT_STALE_SEQUENCE) {
            tracker->stale_counter += 1U;
        }
        return false;
    }

    tracker->valid = true;
    tracker->fresh = true;
    tracker->seen_once = true;
    tracker->last_rx_time_ms = now_ms;
    tracker->effective_timeout_ms =
        (uint16_t)command->supervision_timeout_100ms * 100U;
    tracker->last_sequence = command->command_sequence;
    tracker->last_command = *command;
    tracker->last_rejection = CLUSTER_COMMAND_REJECT_NONE;
    tracker->accepted_counter += 1U;
    return true;
}

bool cluster_command_tracker_tick(cluster_command_tracker_t *tracker,
                                  uint32_t now_ms) {
    if (!tracker->valid || !tracker->fresh) {
        return false;
    }

    if (cluster_elapsed_ms(now_ms, tracker->last_rx_time_ms) >
        tracker->effective_timeout_ms) {
        tracker->fresh = false;
        tracker->timeout_counter += 1U;
        tracker->last_rejection = CLUSTER_COMMAND_REJECT_TIMEOUT;
        return true;
    }

    return false;
}

uint32_t cluster_command_tracker_age_ms(const cluster_command_tracker_t *tracker,
                                        uint32_t now_ms) {
    if (!tracker->valid) {
        return UINT32_MAX;
    }

    return cluster_elapsed_ms(now_ms, tracker->last_rx_time_ms);
}

bool cluster_command_tracker_is_fresh(const cluster_command_tracker_t *tracker) {
    return tracker->valid && tracker->fresh;
}

