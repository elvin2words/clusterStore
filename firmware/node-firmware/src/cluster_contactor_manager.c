#include "cluster_contactor_manager.h"

static uint32_t cluster_elapsed_ms(uint32_t now_ms, uint32_t then_ms) {
    return now_ms >= then_ms
               ? (now_ms - then_ms)
               : (UINT32_MAX - then_ms + now_ms + 1U);
}

static uint16_t cluster_voltage_delta_mv(uint16_t pack_voltage_mv,
                                         uint16_t bus_voltage_mv) {
    return pack_voltage_mv >= bus_voltage_mv
               ? (uint16_t)(pack_voltage_mv - bus_voltage_mv)
               : (uint16_t)(bus_voltage_mv - pack_voltage_mv);
}

static void cluster_contactor_enter(cluster_contactor_manager_t *manager,
                                    cluster_contactor_state_t next_state,
                                    uint32_t now_ms) {
    manager->state = next_state;
    manager->state_entered_at_ms = now_ms;
}

void cluster_contactor_manager_init(cluster_contactor_manager_t *manager,
                                    const cluster_contactor_config_t *config,
                                    uint32_t now_ms) {
    manager->config = *config;
    manager->state = CLUSTER_CONTACTOR_OPEN;
    manager->state_entered_at_ms = now_ms;
    manager->welded_fault_latched = false;
    manager->feedback_fault_latched = false;
}

void cluster_contactor_manager_clear_faults(cluster_contactor_manager_t *manager,
                                            uint32_t now_ms) {
    manager->welded_fault_latched = false;
    manager->feedback_fault_latched = false;
    cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPEN, now_ms);
}

void cluster_contactor_manager_step(cluster_contactor_manager_t *manager,
                                    const cluster_contactor_inputs_t *inputs,
                                    uint32_t now_ms,
                                    cluster_contactor_outputs_t *outputs) {
    uint16_t voltage_delta_mv;
    voltage_delta_mv =
        cluster_voltage_delta_mv(inputs->pack_voltage_mv, inputs->bus_voltage_mv);

    if (manager->welded_fault_latched) {
        manager->state = CLUSTER_CONTACTOR_WELDED_FAULT;
    }

    switch (manager->state) {
        case CLUSTER_CONTACTOR_OPEN:
            if (inputs->main_feedback_closed) {
                manager->welded_fault_latched = true;
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_WELDED_FAULT, now_ms);
            } else if (inputs->request_closed && !inputs->inhibit_close) {
                if (voltage_delta_mv <= manager->config.voltage_match_window_mv) {
                    cluster_contactor_enter(manager, CLUSTER_CONTACTOR_MAIN_CLOSING, now_ms);
                } else {
                    cluster_contactor_enter(manager, CLUSTER_CONTACTOR_PRECHARGE, now_ms);
                }
            }
            break;

        case CLUSTER_CONTACTOR_PRECHARGE:
            if (!inputs->request_closed || inputs->inhibit_close) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            } else if (voltage_delta_mv <= manager->config.voltage_match_window_mv) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_MAIN_CLOSING, now_ms);
            } else if (cluster_elapsed_ms(now_ms, manager->state_entered_at_ms) >
                       manager->config.precharge_timeout_ms) {
                manager->feedback_fault_latched = true;
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            }
            break;

        case CLUSTER_CONTACTOR_MAIN_CLOSING:
            if (!inputs->request_closed || inputs->inhibit_close) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            } else if (inputs->main_feedback_closed) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_CLOSED, now_ms);
            } else if (cluster_elapsed_ms(now_ms, manager->state_entered_at_ms) >
                       manager->config.close_timeout_ms) {
                manager->feedback_fault_latched = true;
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            }
            break;

        case CLUSTER_CONTACTOR_CLOSED:
            if (!inputs->request_closed || inputs->inhibit_close) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            } else if (!inputs->main_feedback_closed) {
                manager->feedback_fault_latched = true;
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPENING, now_ms);
            }
            break;

        case CLUSTER_CONTACTOR_OPENING:
            if (!inputs->main_feedback_closed && !inputs->precharge_feedback_closed) {
                cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPEN, now_ms);
            } else if (cluster_elapsed_ms(now_ms, manager->state_entered_at_ms) >
                       manager->config.open_timeout_ms) {
                if (inputs->main_feedback_closed) {
                    manager->welded_fault_latched = true;
                    cluster_contactor_enter(manager, CLUSTER_CONTACTOR_WELDED_FAULT, now_ms);
                } else {
                    cluster_contactor_enter(manager, CLUSTER_CONTACTOR_OPEN, now_ms);
                }
            }
            break;

        case CLUSTER_CONTACTOR_WELDED_FAULT:
        default:
            break;
    }

    outputs->state = manager->state;
    outputs->drive_precharge = manager->state == CLUSTER_CONTACTOR_PRECHARGE ||
                               manager->state == CLUSTER_CONTACTOR_MAIN_CLOSING;
    outputs->drive_main_contactor = manager->state == CLUSTER_CONTACTOR_MAIN_CLOSING ||
                                    manager->state == CLUSTER_CONTACTOR_CLOSED;
    outputs->precharge_complete =
        voltage_delta_mv <= manager->config.voltage_match_window_mv;
    outputs->ready_for_connection = manager->state == CLUSTER_CONTACTOR_CLOSED &&
                                    inputs->main_feedback_closed;
    outputs->welded_fault = manager->welded_fault_latched;
    outputs->feedback_fault = manager->feedback_fault_latched;
}

