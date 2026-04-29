#include "cs_native_node_runtime.h"
#include <stddef.h>
#include <string.h>

void cs_native_node_runtime_config_init(cs_native_node_runtime_config_t *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->node_address = 1U;
    config->default_active_slot_id = CLUSTER_BOOT_SLOT_A;
    config->max_trial_boot_attempts = 3U;
    config->status_period_ms = 500U;
    config->command_poll_budget = 4U;
    config->journal_record_capacity = 512U;
    config->default_firmware_version = "0.1.0";
    config->flash_layout.flash_base_address = CS_G474_BOOTLOADER_ADDRESS;
    config->flash_layout.flash_size_bytes = CS_G474_FLASH_SIZE_BYTES;
    config->flash_layout.bootloader_size_bytes = 32UL * 1024UL;
    config->flash_layout.metadata_size_bytes = 8UL * 1024UL;
    config->flash_layout.journal_size_bytes = 24UL * 1024UL;
    config->flash_layout.slot_size_bytes = 192UL * 1024UL;
    config->flash_layout.bootloader_address = CS_G474_BOOTLOADER_ADDRESS;
    config->flash_layout.slot_a_address = CS_G474_SLOT_A_ADDRESS;
    config->flash_layout.slot_b_address = CS_G474_SLOT_B_ADDRESS;
    config->flash_layout.metadata_address = CS_G474_BCB_A_ADDRESS;
    config->flash_layout.journal_address = CS_G474_JOURNAL_ADDRESS;
    config->controller.max_supervision_timeout_ms = 2000U;
    config->controller.max_charge_setpoint_da = 500;
    config->controller.max_discharge_setpoint_da = 500;
    config->controller.current_ramp_up_da_per_s = 100U;
    config->controller.current_ramp_down_da_per_s = 150U;
    config->controller.contactor_config.voltage_match_window_mv = 500U;
    config->controller.contactor_config.precharge_timeout_ms = 3000U;
    config->controller.contactor_config.close_timeout_ms = 1500U;
    config->controller.contactor_config.open_timeout_ms = 1500U;
    config->bridge.default_soc_pct = 50U;
    config->bridge.local_fault_flags = NODE_FAULT_FLAG_NONE;
    config->bridge.balancing_supported = true;
    config->bridge.maintenance_lockout = false;
    config->bridge.service_lockout = false;
}

cs_status_t cs_native_node_runtime_init(cs_native_node_runtime_t *runtime,
                                        cs_bsp_g474_t *bsp,
                                        const cs_native_node_runtime_config_t *config) {
    cluster_node_runtime_config_t runtime_config;
    uint32_t now_ms;

    if (runtime == NULL || bsp == NULL || config == NULL ||
        config->default_firmware_version == NULL ||
        config->journal_record_capacity == 0U ||
        config->journal_record_capacity > CS_NATIVE_NODE_RUNTIME_MAX_JOURNAL_RECORDS) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->bsp = bsp;
    if (cs_cluster_bridge_g474_init(&runtime->bridge, bsp, &config->bridge) !=
        CS_STATUS_OK) {
        return CS_STATUS_ERROR;
    }

    cs_cluster_bridge_g474_bind_platform(&runtime->bridge, &runtime->cluster_platform);
    memset(&runtime_config, 0, sizeof(runtime_config));
    runtime_config.node_address = config->node_address;
    runtime_config.default_active_slot_id = config->default_active_slot_id;
    runtime_config.max_trial_boot_attempts = config->max_trial_boot_attempts;
    runtime_config.status_period_ms = config->status_period_ms;
    runtime_config.command_poll_budget = config->command_poll_budget;
    runtime_config.journal_record_capacity = config->journal_record_capacity;
    runtime_config.default_firmware_version = config->default_firmware_version;
    runtime_config.flash_layout = config->flash_layout;
    runtime_config.controller = config->controller;
    now_ms = cluster_platform_now_ms(&runtime->cluster_platform);
    if (!cluster_node_runtime_init(&runtime->runtime,
                                   &runtime->cluster_platform,
                                   &runtime_config,
                                   runtime->journal_records,
                                   now_ms)) {
        return CS_STATUS_ERROR;
    }

    return CS_STATUS_OK;
}

cs_status_t cs_native_node_runtime_step(cs_native_node_runtime_t *runtime) {
    if (runtime == NULL || runtime->bsp == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (runtime->bsp->config.can.enable_rx_fifo0_irq == 0U) {
        (void)cs_bsp_g474_on_fdcan_rx_fifo0_irq(runtime->bsp);
    }

    return cluster_node_runtime_step(&runtime->runtime, &runtime->last_output)
               ? CS_STATUS_OK
               : CS_STATUS_ERROR;
}

const cluster_node_control_output_t *cs_native_node_runtime_last_output(
    const cs_native_node_runtime_t *runtime) {
    return runtime == NULL ? NULL : &runtime->last_output;
}
