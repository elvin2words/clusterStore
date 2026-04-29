#include "cluster_node_runtime.h"
#include <stddef.h>
#include <string.h>

static uint32_t cluster_runtime_elapsed_ms(uint32_t now_ms, uint32_t then_ms) {
    return now_ms >= then_ms
               ? (now_ms - then_ms)
               : (UINT32_MAX - then_ms + now_ms + 1U);
}

static bool cluster_node_runtime_sync_boot_state(cluster_node_runtime_t *runtime) {
    cluster_boot_control_from_ota_manager(&runtime->persistent_state.boot_control,
                                          &runtime->flash_layout,
                                          &runtime->ota_manager);
    return cluster_persistent_state_save(&runtime->persistent_state);
}

static bool cluster_node_runtime_publish_status(
    cluster_node_runtime_t *runtime,
    const cluster_node_measurements_t *measurements,
    const cluster_node_control_output_t *output) {
    cluster_can_frame_t frame;
    node_status_payload_t payload;

    memset(&frame, 0, sizeof(frame));
    cluster_node_controller_build_status_payload(measurements, output, &payload);
    frame.id = cluster_resolve_can_id(NODE_STATUS_BASE_ID, runtime->config.node_address);
    frame.dlc = NODE_STATUS_PAYLOAD_BYTES;
    if (!cluster_encode_status_payload(&payload, frame.data)) {
        return false;
    }

    return cluster_platform_can_send(&runtime->platform, &frame) ==
           CLUSTER_PLATFORM_STATUS_OK;
}

static void cluster_node_runtime_drain_commands(
    cluster_node_runtime_t *runtime,
    const cluster_node_measurements_t *measurements) {
    cluster_can_frame_t frame;
    node_command_payload_t command;
    uint16_t budget;
    uint16_t handled_frames = 0U;

    budget = runtime->config.command_poll_budget == 0U
                 ? 4U
                 : runtime->config.command_poll_budget;
    while (handled_frames < budget &&
           cluster_platform_can_receive(&runtime->platform, &frame)) {
        handled_frames += 1U;
        if (frame.id !=
                cluster_resolve_can_id(NODE_COMMAND_BASE_ID,
                                       runtime->config.node_address) ||
            frame.dlc < NODE_COMMAND_PAYLOAD_BYTES) {
            continue;
        }

        if (cluster_decode_command_payload(frame.data, &command)) {
            (void)cluster_node_controller_receive_command(&runtime->controller,
                                                          &command,
                                                          measurements);
            runtime->status_dirty = true;
        }
    }
}

static bool cluster_node_runtime_apply_outputs(
    cluster_node_runtime_t *runtime,
    const cluster_node_control_output_t *output) {
    if (cluster_platform_set_precharge_drive(&runtime->platform,
                                             output->drive_precharge) !=
            CLUSTER_PLATFORM_STATUS_OK ||
        cluster_platform_set_main_contactor_drive(&runtime->platform,
                                                  output->drive_main_contactor) !=
            CLUSTER_PLATFORM_STATUS_OK) {
        return false;
    }

    return cluster_platform_watchdog_kick(&runtime->platform) ==
               CLUSTER_PLATFORM_STATUS_OK ||
           runtime->platform.api == NULL ||
           runtime->platform.api->watchdog_kick == NULL;
}

bool cluster_node_runtime_init(cluster_node_runtime_t *runtime,
                               const cluster_platform_t *platform,
                               const cluster_node_runtime_config_t *config,
                               cluster_event_record_t *journal_records,
                               uint32_t now_ms) {
    if (runtime == NULL || platform == NULL || config == NULL ||
        journal_records == NULL || config->journal_record_capacity == 0U ||
        config->default_firmware_version == NULL) {
        return false;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->platform = *platform;
    runtime->config = *config;
    runtime->journal_records = journal_records;

    if (!cluster_flash_layout_build(&config->flash_layout, &runtime->flash_layout) ||
        !cluster_persistent_state_is_layout_compatible(
            &runtime->flash_layout,
            config->journal_record_capacity)) {
        return false;
    }

    cluster_persistent_state_init(&runtime->persistent_state,
                                  &runtime->platform,
                                  &runtime->flash_layout,
                                  config->journal_record_capacity);
    if (!cluster_persistent_state_load(&runtime->persistent_state,
                                       config->default_active_slot_id,
                                       config->default_firmware_version)) {
        return false;
    }

    cluster_event_journal_init(&runtime->journal,
                               journal_records,
                               config->journal_record_capacity,
                               cluster_persistent_state_flush_journal,
                               &runtime->persistent_state);
    if (!cluster_persistent_state_restore_journal(&runtime->persistent_state,
                                                  &runtime->journal)) {
        cluster_event_journal_restore(&runtime->journal,
                                      &runtime->persistent_state.journal_metadata);
    }

    cluster_boot_control_to_ota_manager(&runtime->persistent_state.boot_control,
                                        &runtime->ota_manager);
    cluster_node_controller_init(&runtime->controller,
                                 &config->controller,
                                 &runtime->journal,
                                 &runtime->ota_manager,
                                 now_ms);
    runtime->last_status_publish_ms = now_ms;
    runtime->status_dirty = true;
    return true;
}

bool cluster_node_runtime_step(cluster_node_runtime_t *runtime,
                               cluster_node_control_output_t *output) {
    cluster_node_measurements_t measurements;
    uint32_t now_ms;

    if (runtime == NULL || output == NULL) {
        return false;
    }

    memset(&measurements, 0, sizeof(measurements));
    if (cluster_platform_sample_measurements(&runtime->platform, &measurements) !=
        CLUSTER_PLATFORM_STATUS_OK) {
        return false;
    }

    now_ms = cluster_platform_now_ms(&runtime->platform);
    measurements.timestamp_ms = now_ms;
    measurements.main_contactor_feedback_closed =
        cluster_platform_read_main_contactor_feedback(&runtime->platform);
    measurements.precharge_feedback_closed =
        cluster_platform_read_precharge_feedback(&runtime->platform);

    cluster_node_runtime_drain_commands(runtime, &measurements);
    cluster_node_controller_step(&runtime->controller, &measurements, output);
    if (!cluster_node_runtime_apply_outputs(runtime, output)) {
        return false;
    }

    if (runtime->status_dirty ||
        cluster_runtime_elapsed_ms(now_ms, runtime->last_status_publish_ms) >=
            runtime->config.status_period_ms) {
        if (!cluster_node_runtime_publish_status(runtime, &measurements, output)) {
            return false;
        }

        runtime->last_status_publish_ms = now_ms;
        runtime->status_dirty = false;
    }

    return true;
}

bool cluster_node_runtime_register_ota_candidate(
    cluster_node_runtime_t *runtime,
    uint8_t slot_id,
    const char *version,
    uint32_t image_size_bytes,
    uint32_t image_crc32) {
    uint32_t now_ms;

    if (runtime == NULL) {
        return false;
    }

    now_ms = cluster_platform_now_ms(&runtime->platform);
    if (!cluster_node_controller_register_ota_candidate(&runtime->controller,
                                                        now_ms,
                                                        slot_id,
                                                        version,
                                                        image_size_bytes,
                                                        image_crc32)) {
        return false;
    }

    return cluster_node_runtime_sync_boot_state(runtime);
}

bool cluster_node_runtime_activate_ota_candidate(cluster_node_runtime_t *runtime) {
    uint32_t now_ms;
    cluster_platform_status_t status;

    if (runtime == NULL) {
        return false;
    }

    now_ms = cluster_platform_now_ms(&runtime->platform);
    if (!cluster_node_controller_activate_ota_candidate(
            &runtime->controller,
            now_ms,
            runtime->config.max_trial_boot_attempts) ||
        !cluster_node_runtime_sync_boot_state(runtime)) {
        return false;
    }

    status = cluster_platform_schedule_boot_slot(&runtime->platform,
                                                 runtime->ota_manager.active_slot.slot_id);
    return status == CLUSTER_PLATFORM_STATUS_OK ||
           status == CLUSTER_PLATFORM_STATUS_UNSUPPORTED;
}

bool cluster_node_runtime_confirm_ota(cluster_node_runtime_t *runtime) {
    uint32_t now_ms;

    if (runtime == NULL) {
        return false;
    }

    now_ms = cluster_platform_now_ms(&runtime->platform);
    if (!cluster_node_controller_confirm_ota(&runtime->controller, now_ms)) {
        return false;
    }

    return cluster_node_runtime_sync_boot_state(runtime);
}

bool cluster_node_runtime_request_ota_rollback(cluster_node_runtime_t *runtime) {
    uint32_t now_ms;

    if (runtime == NULL) {
        return false;
    }

    now_ms = cluster_platform_now_ms(&runtime->platform);
    if (!cluster_node_controller_request_ota_rollback(&runtime->controller, now_ms)) {
        return false;
    }

    return cluster_node_runtime_sync_boot_state(runtime);
}
