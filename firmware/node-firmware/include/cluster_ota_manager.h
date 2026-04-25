#ifndef CLUSTER_OTA_MANAGER_H
#define CLUSTER_OTA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define CLUSTER_OTA_VERSION_TEXT_BYTES 24U

typedef enum {
    CLUSTER_OTA_SLOT_EMPTY = 0,
    CLUSTER_OTA_SLOT_DOWNLOADED = 1,
    CLUSTER_OTA_SLOT_PENDING_TEST = 2,
    CLUSTER_OTA_SLOT_CONFIRMED = 3,
    CLUSTER_OTA_SLOT_INVALID = 4
} cluster_ota_slot_state_t;

typedef struct {
    uint8_t slot_id;
    uint8_t remaining_boot_attempts;
    bool rollback_requested;
    cluster_ota_slot_state_t state;
    char version[CLUSTER_OTA_VERSION_TEXT_BYTES];
    uint32_t image_size_bytes;
    uint32_t image_crc32;
} cluster_ota_slot_t;

typedef struct {
    cluster_ota_slot_t active_slot;
    cluster_ota_slot_t candidate_slot;
    cluster_ota_slot_t rollback_slot;
} cluster_ota_manager_t;

void cluster_ota_manager_init(cluster_ota_manager_t *manager);
void cluster_ota_manager_set_confirmed_active(cluster_ota_manager_t *manager,
                                              uint8_t slot_id,
                                              const char *version,
                                              uint32_t image_size_bytes,
                                              uint32_t image_crc32);
bool cluster_ota_manager_register_candidate(cluster_ota_manager_t *manager,
                                            uint8_t slot_id,
                                            const char *version,
                                            uint32_t image_size_bytes,
                                            uint32_t image_crc32);
bool cluster_ota_manager_activate_candidate(cluster_ota_manager_t *manager,
                                            uint8_t max_trial_boot_attempts);
bool cluster_ota_manager_note_boot_attempt(cluster_ota_manager_t *manager);
bool cluster_ota_manager_confirm_active(cluster_ota_manager_t *manager);
bool cluster_ota_manager_request_rollback(cluster_ota_manager_t *manager);
bool cluster_ota_manager_should_rollback(const cluster_ota_manager_t *manager);
const cluster_ota_slot_t *cluster_ota_manager_boot_slot(
    const cluster_ota_manager_t *manager);

#endif

