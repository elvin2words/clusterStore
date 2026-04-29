#include "cluster_persistent_state.h"
#include <stddef.h>
#include <string.h>

static uint32_t cluster_persistent_state_crc(
    const cluster_persistent_state_image_t *image) {
    return cluster_crc32_compute(image,
                                 (uint32_t)offsetof(cluster_persistent_state_image_t,
                                                    crc32));
}

static uint32_t cluster_persistent_state_copy_size(
    const cluster_flash_layout_t *layout) {
    if (layout == NULL) {
        return 0U;
    }

    return layout->metadata_size_bytes / CLUSTER_PERSISTENT_STATE_COPY_COUNT;
}

static uint32_t cluster_persistent_state_copy_address(
    const cluster_flash_layout_t *layout,
    uint8_t copy_index) {
    return layout->metadata_address +
           (cluster_persistent_state_copy_size(layout) * copy_index);
}

static bool cluster_persistent_state_read_image(
    const cluster_persistent_state_t *state,
    uint8_t copy_index,
    cluster_persistent_state_image_t *image) {
    if (state == NULL || state->platform == NULL || state->layout == NULL ||
        image == NULL || copy_index >= CLUSTER_PERSISTENT_STATE_COPY_COUNT) {
        return false;
    }

    return cluster_platform_flash_read(state->platform,
                                       cluster_persistent_state_copy_address(
                                           state->layout,
                                           copy_index),
                                       image,
                                       (uint32_t)sizeof(*image)) ==
           CLUSTER_PLATFORM_STATUS_OK;
}

static bool cluster_persistent_state_validate_image(
    const cluster_persistent_state_image_t *image) {
    if (image == NULL || image->magic != CLUSTER_PERSISTENT_STATE_MAGIC ||
        image->version != CLUSTER_PERSISTENT_STATE_VERSION ||
        image->length_bytes != sizeof(*image) ||
        image->journal_capacity == 0U ||
        !cluster_boot_control_validate(&image->boot_control)) {
        return false;
    }

    return cluster_persistent_state_crc(image) == image->crc32;
}

static void cluster_persistent_state_build_image(
    const cluster_persistent_state_t *state,
    cluster_persistent_state_image_t *image) {
    memset(image, 0, sizeof(*image));
    image->magic = CLUSTER_PERSISTENT_STATE_MAGIC;
    image->version = CLUSTER_PERSISTENT_STATE_VERSION;
    image->length_bytes = (uint16_t)sizeof(*image);
    image->generation = state->generation;
    image->boot_control = state->boot_control;
    image->journal_metadata = state->journal_metadata;
    image->journal_capacity = state->journal_capacity;
    image->crc32 = cluster_persistent_state_crc(image);
}

void cluster_persistent_state_init(cluster_persistent_state_t *state,
                                   const cluster_platform_t *platform,
                                   const cluster_flash_layout_t *layout,
                                   uint16_t journal_capacity) {
    if (state == NULL) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->platform = platform;
    state->layout = layout;
    state->journal_capacity = journal_capacity;
}

uint32_t cluster_persistent_state_required_metadata_bytes(void) {
    return (uint32_t)(sizeof(cluster_persistent_state_image_t) *
                      CLUSTER_PERSISTENT_STATE_COPY_COUNT);
}

bool cluster_persistent_state_is_layout_compatible(
    const cluster_flash_layout_t *layout,
    uint16_t journal_capacity) {
    uint32_t journal_bytes;

    if (!cluster_flash_layout_is_valid(layout) || journal_capacity == 0U ||
        layout->metadata_size_bytes <
            cluster_persistent_state_required_metadata_bytes()) {
        return false;
    }

    journal_bytes = (uint32_t)journal_capacity * sizeof(cluster_event_record_t);
    return journal_bytes <= layout->journal_size_bytes;
}

bool cluster_persistent_state_load(cluster_persistent_state_t *state,
                                   uint8_t default_active_slot_id,
                                   const char *default_version) {
    cluster_persistent_state_image_t image;
    cluster_persistent_state_image_t selected_image;
    bool found_image = false;
    uint8_t copy_index;

    if (state == NULL || state->platform == NULL || state->layout == NULL ||
        !cluster_persistent_state_is_layout_compatible(state->layout,
                                                       state->journal_capacity)) {
        return false;
    }

    memset(&selected_image, 0, sizeof(selected_image));
    for (copy_index = 0U; copy_index < CLUSTER_PERSISTENT_STATE_COPY_COUNT;
         copy_index += 1U) {
        if (!cluster_persistent_state_read_image(state, copy_index, &image) ||
            !cluster_persistent_state_validate_image(&image)) {
            continue;
        }

        if (!found_image || image.generation >= selected_image.generation) {
            selected_image = image;
            state->active_copy_index = copy_index;
            found_image = true;
        }
    }

    if (!found_image) {
        memset(&state->journal_metadata, 0, sizeof(state->journal_metadata));
        state->generation = 0U;
        state->active_copy_index = 0U;
        cluster_boot_control_init(&state->boot_control,
                                  state->layout,
                                  default_active_slot_id,
                                  default_version);
        return true;
    }

    state->boot_control = selected_image.boot_control;
    state->journal_metadata = selected_image.journal_metadata;
    state->generation = selected_image.generation;
    state->journal_capacity = selected_image.journal_capacity;
    return true;
}

bool cluster_persistent_state_save(cluster_persistent_state_t *state) {
    cluster_persistent_state_image_t image;
    uint8_t target_copy_index;
    uint32_t target_address;
    uint32_t copy_size;

    if (state == NULL || state->platform == NULL || state->layout == NULL ||
        !cluster_persistent_state_is_layout_compatible(state->layout,
                                                       state->journal_capacity)) {
        return false;
    }

    target_copy_index = (uint8_t)((state->active_copy_index + 1U) %
                                  CLUSTER_PERSISTENT_STATE_COPY_COUNT);
    target_address =
        cluster_persistent_state_copy_address(state->layout, target_copy_index);
    copy_size = cluster_persistent_state_copy_size(state->layout);
    state->generation += 1U;
    cluster_boot_control_update_crc(&state->boot_control);
    cluster_persistent_state_build_image(state, &image);

    if (cluster_platform_flash_erase(state->platform, target_address, copy_size) !=
            CLUSTER_PLATFORM_STATUS_OK ||
        cluster_platform_flash_write(state->platform,
                                     target_address,
                                     &image,
                                     (uint32_t)sizeof(image)) !=
            CLUSTER_PLATFORM_STATUS_OK) {
        state->generation -= 1U;
        return false;
    }

    state->active_copy_index = target_copy_index;
    return true;
}

bool cluster_persistent_state_restore_journal(cluster_persistent_state_t *state,
                                              cluster_event_journal_t *journal) {
    uint32_t journal_bytes;

    if (state == NULL || journal == NULL || journal->capacity == 0U ||
        journal->records == NULL || journal->capacity != state->journal_capacity) {
        return false;
    }

    journal_bytes = (uint32_t)journal->capacity * sizeof(cluster_event_record_t);
    if (journal_bytes > state->layout->journal_size_bytes) {
        return false;
    }

    if (cluster_platform_flash_read(state->platform,
                                    state->layout->journal_address,
                                    journal->records,
                                    journal_bytes) !=
        CLUSTER_PLATFORM_STATUS_OK) {
        return false;
    }

    cluster_event_journal_restore(journal, &state->journal_metadata);
    return true;
}

bool cluster_persistent_state_flush_journal(
    void *context,
    const cluster_event_record_t *records,
    uint16_t capacity,
    const cluster_event_journal_metadata_t *metadata) {
    cluster_persistent_state_t *state;
    uint32_t journal_bytes;

    state = (cluster_persistent_state_t *)context;
    if (state == NULL || records == NULL || metadata == NULL || capacity == 0U ||
        capacity != state->journal_capacity) {
        return false;
    }

    journal_bytes = (uint32_t)capacity * sizeof(cluster_event_record_t);
    if (journal_bytes > state->layout->journal_size_bytes) {
        return false;
    }

    if (cluster_platform_flash_erase(state->platform,
                                     state->layout->journal_address,
                                     state->layout->journal_size_bytes) !=
            CLUSTER_PLATFORM_STATUS_OK ||
        cluster_platform_flash_write(state->platform,
                                     state->layout->journal_address,
                                     records,
                                     journal_bytes) !=
            CLUSTER_PLATFORM_STATUS_OK) {
        return false;
    }

    state->journal_metadata = *metadata;
    return cluster_persistent_state_save(state);
}
