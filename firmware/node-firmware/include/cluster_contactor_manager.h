#ifndef CLUSTER_CONTACTOR_MANAGER_H
#define CLUSTER_CONTACTOR_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CLUSTER_CONTACTOR_OPEN = 0,
    CLUSTER_CONTACTOR_PRECHARGE = 1,
    CLUSTER_CONTACTOR_MAIN_CLOSING = 2,
    CLUSTER_CONTACTOR_CLOSED = 3,
    CLUSTER_CONTACTOR_OPENING = 4,
    CLUSTER_CONTACTOR_WELDED_FAULT = 5
} cluster_contactor_state_t;

typedef struct {
    uint16_t voltage_match_window_mv;
    uint16_t precharge_timeout_ms;
    uint16_t close_timeout_ms;
    uint16_t open_timeout_ms;
} cluster_contactor_config_t;

typedef struct {
    bool request_closed;
    bool inhibit_close;
    bool main_feedback_closed;
    bool precharge_feedback_closed;
    uint16_t pack_voltage_mv;
    uint16_t bus_voltage_mv;
} cluster_contactor_inputs_t;

typedef struct {
    cluster_contactor_state_t state;
    bool drive_precharge;
    bool drive_main_contactor;
    bool ready_for_connection;
    bool precharge_complete;
    bool welded_fault;
    bool feedback_fault;
} cluster_contactor_outputs_t;

typedef struct {
    cluster_contactor_config_t config;
    cluster_contactor_state_t state;
    uint32_t state_entered_at_ms;
    bool welded_fault_latched;
    bool feedback_fault_latched;
} cluster_contactor_manager_t;

void cluster_contactor_manager_init(cluster_contactor_manager_t *manager,
                                    const cluster_contactor_config_t *config,
                                    uint32_t now_ms);
void cluster_contactor_manager_clear_faults(cluster_contactor_manager_t *manager,
                                            uint32_t now_ms);
void cluster_contactor_manager_step(cluster_contactor_manager_t *manager,
                                    const cluster_contactor_inputs_t *inputs,
                                    uint32_t now_ms,
                                    cluster_contactor_outputs_t *outputs);

#endif

