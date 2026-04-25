#ifndef CLUSTER_EVENT_JOURNAL_H
#define CLUSTER_EVENT_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CLUSTER_EVENT_SEVERITY_INFO = 0,
    CLUSTER_EVENT_SEVERITY_WARNING = 1,
    CLUSTER_EVENT_SEVERITY_CRITICAL = 2
} cluster_event_severity_t;

typedef enum {
    CLUSTER_EVENT_BOOT = 1,
    CLUSTER_EVENT_COMMAND_ACCEPTED = 2,
    CLUSTER_EVENT_COMMAND_REJECTED = 3,
    CLUSTER_EVENT_COMMAND_TIMEOUT = 4,
    CLUSTER_EVENT_CONTACTOR_TRANSITION = 5,
    CLUSTER_EVENT_CONTACTOR_WELDED = 6,
    CLUSTER_EVENT_MAINTENANCE_LOCKOUT = 7,
    CLUSTER_EVENT_SERVICE_LOCKOUT = 8,
    CLUSTER_EVENT_OTA_CANDIDATE = 9,
    CLUSTER_EVENT_OTA_TRIAL = 10,
    CLUSTER_EVENT_OTA_CONFIRMED = 11,
    CLUSTER_EVENT_OTA_ROLLBACK = 12
} cluster_event_code_t;

typedef struct {
    uint32_t sequence;
    uint32_t timestamp_ms;
    uint16_t event_code;
    uint8_t severity;
    uint8_t detail;
    int32_t value_a;
    int32_t value_b;
} cluster_event_record_t;

typedef struct {
    uint16_t head;
    uint16_t count;
    uint32_t next_sequence;
} cluster_event_journal_metadata_t;

typedef bool (*cluster_event_journal_flush_fn)(
    void *context,
    const cluster_event_record_t *records,
    uint16_t capacity,
    const cluster_event_journal_metadata_t *metadata);

typedef struct {
    cluster_event_record_t *records;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
    uint32_t next_sequence;
    cluster_event_journal_flush_fn flush;
    void *flush_context;
} cluster_event_journal_t;

void cluster_event_journal_init(cluster_event_journal_t *journal,
                                cluster_event_record_t *records,
                                uint16_t capacity,
                                cluster_event_journal_flush_fn flush,
                                void *flush_context);
void cluster_event_journal_restore(cluster_event_journal_t *journal,
                                   const cluster_event_journal_metadata_t *metadata);
bool cluster_event_journal_append(cluster_event_journal_t *journal,
                                  const cluster_event_record_t *record);
bool cluster_event_journal_read(const cluster_event_journal_t *journal,
                                uint16_t index_from_oldest,
                                cluster_event_record_t *record);
bool cluster_event_journal_latest(const cluster_event_journal_t *journal,
                                  cluster_event_record_t *record);
cluster_event_journal_metadata_t cluster_event_journal_metadata(
    const cluster_event_journal_t *journal);

#endif

