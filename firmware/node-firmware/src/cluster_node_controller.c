#include "cluster_node_controller.h"
#include <stddef.h>

static uint32_t cluster_elapsed_ms(uint32_t now_ms, uint32_t then_ms) {
    return now_ms >= then_ms
               ? (now_ms - then_ms)
               : (UINT32_MAX - then_ms + now_ms + 1U);
}

static void cluster_node_log_event(cluster_node_controller_t *controller,
                                   uint32_t timestamp_ms,
                                   uint16_t event_code,
                                   uint8_t severity,
                                   uint8_t detail,
                                   int32_t value_a,
                                   int32_t value_b) {
    cluster_event_record_t record;
    if (controller->journal == NULL) {
        return;
    }

    record.sequence = 0U;
    record.timestamp_ms = timestamp_ms;
    record.event_code = event_code;
    record.severity = severity;
    record.detail = detail;
    record.value_a = value_a;
    record.value_b = value_b;
    (void)cluster_event_journal_append(controller->journal, &record);
}

static cluster_command_policy_t cluster_node_command_policy(
    const cluster_node_controller_t *controller,
    const cluster_node_measurements_t *measurements) {
    cluster_command_policy_t policy;
    policy.local_fault_active = measurements->local_fault_flags != NODE_FAULT_FLAG_NONE;
    policy.maintenance_lockout = measurements->maintenance_lockout;
    policy.service_lockout = measurements->service_lockout;
    policy.balancing_supported = measurements->balancing_supported;
    policy.max_charge_setpoint_da = controller->config.max_charge_setpoint_da;
    policy.max_discharge_setpoint_da = controller->config.max_discharge_setpoint_da;
    policy.max_supervision_timeout_ms = controller->config.max_supervision_timeout_ms;
    return policy;
}

static uint8_t cluster_node_status_flags(const cluster_node_measurements_t *measurements,
                                         const cluster_node_control_output_t *output) {
    uint8_t status_flags = 0U;
    if (output->contactor_state == CLUSTER_CONTACTOR_CLOSED) {
        status_flags |= NODE_STATUS_FLAG_CONTACTOR_CLOSED;
    }
    if (output->ready_for_connection) {
        status_flags |= NODE_STATUS_FLAG_READY_FOR_CONNECTION;
    }
    if (output->logical_state == NODE_STATE_CLUSTER_BALANCING) {
        status_flags |= NODE_STATUS_FLAG_BALANCING_ACTIVE;
    }
    if (measurements->maintenance_lockout) {
        status_flags |= NODE_STATUS_FLAG_MAINTENANCE_LOCKOUT;
    }
    if (measurements->service_lockout) {
        status_flags |= NODE_STATUS_FLAG_SERVICE_LOCKOUT;
    }
    return status_flags;
}

static uint8_t cluster_node_fault_flags(const cluster_node_measurements_t *measurements,
                                        const cluster_node_control_output_t *output,
                                        bool command_seen) {
    uint8_t fault_flags = measurements->local_fault_flags;
    if (!output->command_fresh && command_seen) {
        fault_flags |= NODE_FAULT_FLAG_COMMUNICATION_TIMEOUT;
    }
    if (output->welded_contactor_detected ||
        output->contactor_state == CLUSTER_CONTACTOR_WELDED_FAULT) {
        fault_flags |= NODE_FAULT_FLAG_CONTACTOR_FEEDBACK;
    }
    if (measurements->maintenance_lockout) {
        fault_flags |= NODE_FAULT_FLAG_MAINTENANCE_LOCKOUT;
    }
    return fault_flags;
}

static int16_t cluster_node_target_current_da(
    const cluster_node_controller_t *controller,
    cluster_state_t logical_state,
    const cluster_contactor_outputs_t *contactor_outputs) {
    if (!cluster_state_allows_current_flow(logical_state) ||
        !contactor_outputs->ready_for_connection ||
        !controller->command_tracker.fresh) {
        return 0;
    }

    switch (logical_state) {
        case NODE_STATE_CLUSTER_SLAVE_CHARGE:
        case NODE_STATE_CLUSTER_BALANCING:
            return controller->command_tracker.last_command.charge_setpoint_da;
        case NODE_STATE_CLUSTER_SLAVE_DISCHARGE:
            return (int16_t)(-controller->command_tracker.last_command.discharge_setpoint_da);
        default:
            return 0;
    }
}

void cluster_node_controller_init(cluster_node_controller_t *controller,
                                  const cluster_node_controller_config_t *config,
                                  cluster_event_journal_t *journal,
                                  cluster_ota_manager_t *ota_manager,
                                  uint32_t now_ms) {
    controller->config = *config;
    controller->journal = journal;
    controller->ota_manager = ota_manager;
    controller->logical_state = NODE_STATE_STANDALONE_SAFE;
    controller->last_contactor_state = CLUSTER_CONTACTOR_OPEN;
    controller->last_maintenance_lockout = false;
    controller->last_service_lockout = false;
    controller->heartbeat_counter = 0U;
    controller->last_step_time_ms = now_ms;

    cluster_command_tracker_init(&controller->command_tracker,
                                 config->max_supervision_timeout_ms);
    cluster_contactor_manager_init(&controller->contactor_manager,
                                   &config->contactor_config,
                                   now_ms);
    cluster_current_ramp_init(&controller->current_ramp,
                              config->current_ramp_up_da_per_s,
                              config->current_ramp_down_da_per_s);

    cluster_node_log_event(controller,
                           now_ms,
                           CLUSTER_EVENT_BOOT,
                           CLUSTER_EVENT_SEVERITY_INFO,
                           0U,
                           CLUSTER_CAN_PROTOCOL_VERSION,
                           0);
}

bool cluster_node_controller_receive_command(
    cluster_node_controller_t *controller,
    const node_command_payload_t *command,
    const cluster_node_measurements_t *measurements) {
    cluster_command_policy_t policy;
    bool accepted;

    policy = cluster_node_command_policy(controller, measurements);
    accepted = cluster_command_tracker_receive(&controller->command_tracker,
                                              command,
                                              &policy,
                                              measurements->timestamp_ms);

    if (accepted) {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               CLUSTER_EVENT_COMMAND_ACCEPTED,
                               CLUSTER_EVENT_SEVERITY_INFO,
                               command->command_sequence,
                               command->mode,
                               command->command_flags);
    } else {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               CLUSTER_EVENT_COMMAND_REJECTED,
                               CLUSTER_EVENT_SEVERITY_WARNING,
                               (uint8_t)controller->command_tracker.last_rejection,
                               command->command_sequence,
                               command->mode);
    }

    return accepted;
}

void cluster_node_controller_step(cluster_node_controller_t *controller,
                                  const cluster_node_measurements_t *measurements,
                                  cluster_node_control_output_t *output) {
    cluster_contactor_inputs_t contactor_inputs;
    cluster_contactor_outputs_t contactor_outputs;
    cluster_state_input_t state_input;
    int16_t target_current_da;
    uint16_t delta_ms;
    bool timed_out;

    delta_ms = (uint16_t)cluster_elapsed_ms(measurements->timestamp_ms,
                                            controller->last_step_time_ms);
    controller->last_step_time_ms = measurements->timestamp_ms;
    controller->heartbeat_counter += 1U;

    timed_out = cluster_command_tracker_tick(&controller->command_tracker,
                                             measurements->timestamp_ms);
    if (timed_out) {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               CLUSTER_EVENT_COMMAND_TIMEOUT,
                               CLUSTER_EVENT_SEVERITY_WARNING,
                               0U,
                               (int32_t)controller->command_tracker.last_sequence,
                               (int32_t)controller->command_tracker.effective_timeout_ms);
    }

    contactor_inputs.request_closed =
        controller->command_tracker.fresh &&
        ((controller->command_tracker.last_command.command_flags &
          NODE_COMMAND_FLAG_CONTACTOR_CLOSED) != 0U) &&
        controller->command_tracker.last_command.mode != NODE_MODE_CLUSTER_ISOLATED;
    contactor_inputs.inhibit_close =
        measurements->local_fault_flags != NODE_FAULT_FLAG_NONE ||
        measurements->maintenance_lockout ||
        measurements->service_lockout ||
        !controller->command_tracker.fresh;
    contactor_inputs.main_feedback_closed =
        measurements->main_contactor_feedback_closed;
    contactor_inputs.precharge_feedback_closed =
        measurements->precharge_feedback_closed;
    contactor_inputs.pack_voltage_mv = measurements->pack_voltage_mv;
    contactor_inputs.bus_voltage_mv = measurements->bus_voltage_mv;

    cluster_contactor_manager_step(&controller->contactor_manager,
                                   &contactor_inputs,
                                   measurements->timestamp_ms,
                                   &contactor_outputs);

    state_input.cluster_command_seen = controller->command_tracker.seen_once;
    state_input.cluster_command_fresh = controller->command_tracker.fresh;
    state_input.cluster_command_rejected =
        controller->command_tracker.last_rejection != CLUSTER_COMMAND_REJECT_NONE &&
        !controller->command_tracker.fresh;
    state_input.local_fault_active =
        measurements->local_fault_flags != NODE_FAULT_FLAG_NONE ||
        contactor_outputs.feedback_fault ||
        contactor_outputs.welded_fault;
    state_input.maintenance_lockout = measurements->maintenance_lockout;
    state_input.service_lockout = measurements->service_lockout;
    state_input.contactor_feedback_ok = !contactor_outputs.feedback_fault;
    state_input.welded_contactor_detected = contactor_outputs.welded_fault;
    state_input.allow_balancing =
        (controller->command_tracker.last_command.command_flags &
         NODE_COMMAND_FLAG_ALLOW_BALANCING) != 0U;
    state_input.requested_mode = controller->command_tracker.fresh
                                     ? (cluster_node_mode_t)controller->command_tracker.last_command.mode
                                     : NODE_MODE_CLUSTER_ISOLATED;

    controller->logical_state =
        cluster_state_machine_step(controller->logical_state, &state_input);

    target_current_da =
        cluster_node_target_current_da(controller,
                                       controller->logical_state,
                                       &contactor_outputs);
    if (cluster_state_requires_open_contactors(controller->logical_state)) {
        target_current_da = 0;
    }

    output->limited_current_da =
        cluster_current_ramp_step(&controller->current_ramp,
                                  target_current_da,
                                  delta_ms);
    output->logical_state = controller->logical_state;
    output->contactor_state = contactor_outputs.state;
    output->drive_precharge = contactor_outputs.drive_precharge;
    output->drive_main_contactor = contactor_outputs.drive_main_contactor;
    output->ready_for_connection = contactor_outputs.ready_for_connection;
    output->precharge_complete = contactor_outputs.precharge_complete;
    output->welded_contactor_detected = contactor_outputs.welded_fault;
    output->command_fresh = controller->command_tracker.fresh;
    output->heartbeat_counter = controller->heartbeat_counter;
    output->accepted_command_counter = controller->command_tracker.accepted_counter;
    output->rejected_command_counter = controller->command_tracker.rejected_counter;
    output->timeout_counter = controller->command_tracker.timeout_counter;
    output->last_command_sequence = controller->command_tracker.last_sequence;
    output->last_rejection = controller->command_tracker.last_rejection;
    output->target_current_da = target_current_da;
    output->status_flags = cluster_node_status_flags(measurements, output);
    output->fault_flags = cluster_node_fault_flags(measurements,
                                                   output,
                                                   controller->command_tracker.seen_once);

    if (controller->last_contactor_state != output->contactor_state) {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               output->contactor_state == CLUSTER_CONTACTOR_WELDED_FAULT
                                   ? CLUSTER_EVENT_CONTACTOR_WELDED
                                   : CLUSTER_EVENT_CONTACTOR_TRANSITION,
                               output->contactor_state == CLUSTER_CONTACTOR_WELDED_FAULT
                                   ? CLUSTER_EVENT_SEVERITY_CRITICAL
                                   : CLUSTER_EVENT_SEVERITY_INFO,
                               (uint8_t)output->contactor_state,
                               (int32_t)controller->last_contactor_state,
                               (int32_t)output->contactor_state);
        controller->last_contactor_state = output->contactor_state;
    }

    if (controller->last_maintenance_lockout != measurements->maintenance_lockout) {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               CLUSTER_EVENT_MAINTENANCE_LOCKOUT,
                               measurements->maintenance_lockout
                                   ? CLUSTER_EVENT_SEVERITY_WARNING
                                   : CLUSTER_EVENT_SEVERITY_INFO,
                               measurements->maintenance_lockout ? 1U : 0U,
                               0,
                               0);
        controller->last_maintenance_lockout = measurements->maintenance_lockout;
    }

    if (controller->last_service_lockout != measurements->service_lockout) {
        cluster_node_log_event(controller,
                               measurements->timestamp_ms,
                               CLUSTER_EVENT_SERVICE_LOCKOUT,
                               measurements->service_lockout
                                   ? CLUSTER_EVENT_SEVERITY_WARNING
                                   : CLUSTER_EVENT_SEVERITY_INFO,
                               measurements->service_lockout ? 1U : 0U,
                               0,
                               0);
        controller->last_service_lockout = measurements->service_lockout;
    }
}

void cluster_node_controller_build_status_payload(
    const cluster_node_measurements_t *measurements,
    const cluster_node_control_output_t *output,
    node_status_payload_t *payload) {
    cluster_build_status_payload(payload,
                                 measurements->soc_pct,
                                 measurements->pack_voltage_mv,
                                 measurements->measured_current_da,
                                 measurements->temperature_c,
                                 output->status_flags,
                                 output->fault_flags);
}

void cluster_node_controller_get_diagnostics(
    const cluster_node_controller_t *controller,
    cluster_node_diagnostics_t *diagnostics) {
    diagnostics->accepted_command_counter = controller->command_tracker.accepted_counter;
    diagnostics->rejected_command_counter = controller->command_tracker.rejected_counter;
    diagnostics->timeout_counter = controller->command_tracker.timeout_counter;
    diagnostics->heartbeat_counter = controller->heartbeat_counter;
    diagnostics->last_command_sequence = controller->command_tracker.last_sequence;
    diagnostics->last_rejection = controller->command_tracker.last_rejection;
    diagnostics->contactor_state = controller->contactor_manager.state;
    diagnostics->logical_state = controller->logical_state;
}

bool cluster_node_controller_register_ota_candidate(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms,
    uint8_t slot_id,
    const char *version,
    uint32_t image_size_bytes,
    uint32_t image_crc32) {
    bool result;
    if (controller->ota_manager == NULL) {
        return false;
    }

    result = cluster_ota_manager_register_candidate(controller->ota_manager,
                                                    slot_id,
                                                    version,
                                                    image_size_bytes,
                                                    image_crc32);
    if (result) {
        cluster_node_log_event(controller,
                               timestamp_ms,
                               CLUSTER_EVENT_OTA_CANDIDATE,
                               CLUSTER_EVENT_SEVERITY_INFO,
                               slot_id,
                               (int32_t)image_size_bytes,
                               (int32_t)image_crc32);
    }
    return result;
}

bool cluster_node_controller_activate_ota_candidate(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms,
    uint8_t max_trial_boot_attempts) {
    bool result;
    if (controller->ota_manager == NULL) {
        return false;
    }

    result = cluster_ota_manager_activate_candidate(controller->ota_manager,
                                                    max_trial_boot_attempts);
    if (result) {
        cluster_node_log_event(controller,
                               timestamp_ms,
                               CLUSTER_EVENT_OTA_TRIAL,
                               CLUSTER_EVENT_SEVERITY_WARNING,
                               max_trial_boot_attempts,
                               controller->ota_manager->active_slot.slot_id,
                               0);
    }
    return result;
}

bool cluster_node_controller_confirm_ota(cluster_node_controller_t *controller,
                                         uint32_t timestamp_ms) {
    bool result;
    if (controller->ota_manager == NULL) {
        return false;
    }

    result = cluster_ota_manager_confirm_active(controller->ota_manager);
    if (result) {
        cluster_node_log_event(controller,
                               timestamp_ms,
                               CLUSTER_EVENT_OTA_CONFIRMED,
                               CLUSTER_EVENT_SEVERITY_INFO,
                               controller->ota_manager->active_slot.slot_id,
                               0,
                               0);
    }
    return result;
}

bool cluster_node_controller_request_ota_rollback(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms) {
    bool result;
    if (controller->ota_manager == NULL) {
        return false;
    }

    result = cluster_ota_manager_request_rollback(controller->ota_manager);
    cluster_node_log_event(controller,
                           timestamp_ms,
                           CLUSTER_EVENT_OTA_ROLLBACK,
                           result ? CLUSTER_EVENT_SEVERITY_WARNING
                                  : CLUSTER_EVENT_SEVERITY_CRITICAL,
                           result ? 1U : 0U,
                           controller->ota_manager->active_slot.slot_id,
                           0);
    return result;
}
