#ifndef CLUSTER_NODE_CONTROLLER_H
#define CLUSTER_NODE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "cluster_can_protocol.h"
#include "cluster_command_manager.h"
#include "cluster_contactor_manager.h"
#include "cluster_current_ramp.h"
#include "cluster_event_journal.h"
#include "cluster_ota_manager.h"
#include "cluster_state_machine.h"

typedef struct {
    uint32_t timestamp_ms;
    uint8_t soc_pct;
    uint16_t pack_voltage_mv;
    uint16_t bus_voltage_mv;
    int16_t measured_current_da;
    int8_t temperature_c;
    uint8_t local_fault_flags;
    bool main_contactor_feedback_closed;
    bool precharge_feedback_closed;
    bool balancing_supported;
    bool maintenance_lockout;
    bool service_lockout;
} cluster_node_measurements_t;

typedef struct {
    uint16_t max_supervision_timeout_ms;
    int16_t max_charge_setpoint_da;
    int16_t max_discharge_setpoint_da;
    uint16_t current_ramp_up_da_per_s;
    uint16_t current_ramp_down_da_per_s;
    cluster_contactor_config_t contactor_config;
} cluster_node_controller_config_t;

typedef struct {
    cluster_state_t logical_state;
    cluster_contactor_state_t contactor_state;
    bool drive_precharge;
    bool drive_main_contactor;
    bool ready_for_connection;
    bool precharge_complete;
    bool welded_contactor_detected;
    bool command_fresh;
    uint8_t heartbeat_counter;
    uint32_t accepted_command_counter;
    uint32_t rejected_command_counter;
    uint32_t timeout_counter;
    uint8_t last_command_sequence;
    cluster_command_rejection_reason_t last_rejection;
    int16_t target_current_da;
    int16_t limited_current_da;
    uint8_t status_flags;
    uint8_t fault_flags;
} cluster_node_control_output_t;

typedef struct {
    uint32_t accepted_command_counter;
    uint32_t rejected_command_counter;
    uint32_t timeout_counter;
    uint8_t heartbeat_counter;
    uint8_t last_command_sequence;
    cluster_command_rejection_reason_t last_rejection;
    cluster_contactor_state_t contactor_state;
    cluster_state_t logical_state;
} cluster_node_diagnostics_t;

typedef struct {
    cluster_node_controller_config_t config;
    cluster_command_tracker_t command_tracker;
    cluster_contactor_manager_t contactor_manager;
    cluster_current_ramp_t current_ramp;
    cluster_event_journal_t *journal;
    cluster_ota_manager_t *ota_manager;
    cluster_state_t logical_state;
    cluster_contactor_state_t last_contactor_state;
    bool last_maintenance_lockout;
    bool last_service_lockout;
    uint8_t heartbeat_counter;
    uint32_t last_step_time_ms;
} cluster_node_controller_t;

void cluster_node_controller_init(cluster_node_controller_t *controller,
                                  const cluster_node_controller_config_t *config,
                                  cluster_event_journal_t *journal,
                                  cluster_ota_manager_t *ota_manager,
                                  uint32_t now_ms);
bool cluster_node_controller_receive_command(
    cluster_node_controller_t *controller,
    const node_command_payload_t *command,
    const cluster_node_measurements_t *measurements);
void cluster_node_controller_step(cluster_node_controller_t *controller,
                                  const cluster_node_measurements_t *measurements,
                                  cluster_node_control_output_t *output);
void cluster_node_controller_build_status_payload(
    const cluster_node_measurements_t *measurements,
    const cluster_node_control_output_t *output,
    node_status_payload_t *payload);
void cluster_node_controller_get_diagnostics(
    const cluster_node_controller_t *controller,
    cluster_node_diagnostics_t *diagnostics);

bool cluster_node_controller_register_ota_candidate(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms,
    uint8_t slot_id,
    const char *version,
    uint32_t image_size_bytes,
    uint32_t image_crc32);
bool cluster_node_controller_activate_ota_candidate(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms,
    uint8_t max_trial_boot_attempts);
bool cluster_node_controller_confirm_ota(cluster_node_controller_t *controller,
                                         uint32_t timestamp_ms);
bool cluster_node_controller_request_ota_rollback(
    cluster_node_controller_t *controller,
    uint32_t timestamp_ms);

#endif

