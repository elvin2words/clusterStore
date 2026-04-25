#include "cluster_event_journal.h"
#include <stddef.h>

static uint16_t cluster_event_index(const cluster_event_journal_t *journal,
                                    uint16_t logical_index) {
    return (uint16_t)((journal->head + logical_index) % journal->capacity);
}

void cluster_event_journal_init(cluster_event_journal_t *journal,
                                cluster_event_record_t *records,
                                uint16_t capacity,
                                cluster_event_journal_flush_fn flush,
                                void *flush_context) {
    journal->records = records;
    journal->capacity = capacity;
    journal->head = 0U;
    journal->count = 0U;
    journal->next_sequence = 1U;
    journal->flush = flush;
    journal->flush_context = flush_context;
}

void cluster_event_journal_restore(cluster_event_journal_t *journal,
                                   const cluster_event_journal_metadata_t *metadata) {
    if (metadata == NULL) {
        return;
    }

    if (journal->capacity == 0U) {
        journal->head = 0U;
        journal->count = 0U;
        journal->next_sequence = 1U;
        return;
    }

    journal->head = (uint16_t)(metadata->head % journal->capacity);
    journal->count = metadata->count > journal->capacity ? journal->capacity : metadata->count;
    journal->next_sequence = metadata->next_sequence == 0U ? 1U : metadata->next_sequence;
}

cluster_event_journal_metadata_t cluster_event_journal_metadata(
    const cluster_event_journal_t *journal) {
    cluster_event_journal_metadata_t metadata;
    metadata.head = journal->head;
    metadata.count = journal->count;
    metadata.next_sequence = journal->next_sequence;
    return metadata;
}

bool cluster_event_journal_append(cluster_event_journal_t *journal,
                                  const cluster_event_record_t *record) {
    uint16_t slot_index;
    cluster_event_record_t stored_record;
    cluster_event_journal_metadata_t metadata;

    if (journal == NULL || record == NULL || journal->records == NULL ||
        journal->capacity == 0U) {
        return false;
    }

    stored_record = *record;
    stored_record.sequence = journal->next_sequence++;

    if (journal->count < journal->capacity) {
        slot_index = cluster_event_index(journal, journal->count);
        journal->count += 1U;
    } else {
        slot_index = journal->head;
        journal->head = (uint16_t)((journal->head + 1U) % journal->capacity);
    }

    journal->records[slot_index] = stored_record;

    if (journal->flush != NULL) {
        metadata = cluster_event_journal_metadata(journal);
        return journal->flush(journal->flush_context,
                              journal->records,
                              journal->capacity,
                              &metadata);
    }

    return true;
}

bool cluster_event_journal_read(const cluster_event_journal_t *journal,
                                uint16_t index_from_oldest,
                                cluster_event_record_t *record) {
    if (journal == NULL || record == NULL || index_from_oldest >= journal->count ||
        journal->capacity == 0U || journal->records == NULL) {
        return false;
    }

    *record = journal->records[cluster_event_index(journal, index_from_oldest)];
    return true;
}

bool cluster_event_journal_latest(const cluster_event_journal_t *journal,
                                  cluster_event_record_t *record) {
    if (journal == NULL || record == NULL || journal->count == 0U) {
        return false;
    }

    return cluster_event_journal_read(journal,
                                      (uint16_t)(journal->count - 1U),
                                      record);
}
