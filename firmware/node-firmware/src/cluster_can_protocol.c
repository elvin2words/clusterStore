#include "cluster_can_protocol.h"
#include <stddef.h>

static void write_u16_le(uint16_t value, uint8_t *bytes) {
    bytes[0] = (uint8_t)(value & 0xFFU);
    bytes[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void write_i16_le(int16_t value, uint8_t *bytes) {
    write_u16_le((uint16_t)value, bytes);
}

static uint16_t read_u16_le(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static int16_t read_i16_le(const uint8_t *bytes) {
    return (int16_t)read_u16_le(bytes);
}

uint16_t cluster_resolve_can_id(uint16_t base_id, uint8_t node_address) {
    return (uint16_t)(base_id + node_address);
}

void cluster_build_status_payload(node_status_payload_t *payload,
                                  uint8_t soc_pct,
                                  uint16_t pack_voltage_mv,
                                  int16_t pack_current_da,
                                  int8_t temperature_c,
                                  uint8_t status_flags,
                                  uint8_t fault_flags) {
    if (payload == NULL) {
        return;
    }

    payload->soc_pct = soc_pct;
    payload->pack_voltage_mv = pack_voltage_mv;
    payload->pack_current_da = pack_current_da;
    payload->temperature_c = temperature_c;
    payload->status_flags = status_flags;
    payload->fault_flags = fault_flags;
}

void cluster_build_command_payload(node_command_payload_t *payload,
                                   cluster_node_mode_t mode,
                                   int16_t charge_setpoint_da,
                                   int16_t discharge_setpoint_da,
                                   uint8_t command_flags,
                                   uint8_t supervision_timeout_100ms,
                                   uint8_t command_sequence) {
    if (payload == NULL) {
        return;
    }

    payload->mode = (uint8_t)mode;
    payload->charge_setpoint_da = charge_setpoint_da;
    payload->discharge_setpoint_da = discharge_setpoint_da;
    payload->command_flags = command_flags;
    payload->supervision_timeout_100ms = supervision_timeout_100ms;
    payload->command_sequence = command_sequence;
}

void cluster_build_diagnostic_chunk(node_diagnostic_chunk_payload_t *payload,
                                    uint8_t sequence,
                                    uint8_t part_index,
                                    uint8_t total_parts,
                                    uint8_t chunk_length,
                                    const uint8_t chunk_bytes[4]) {
    uint8_t index;
    if (payload == NULL) {
        return;
    }

    payload->sequence = sequence;
    payload->part_index = part_index;
    payload->total_parts = total_parts;
    payload->chunk_length = chunk_length;
    for (index = 0U; index < 4U; index += 1U) {
        payload->chunk_bytes[index] = chunk_bytes != NULL ? chunk_bytes[index] : 0U;
    }
}

bool cluster_encode_status_payload(const node_status_payload_t *payload,
                                   uint8_t out_bytes[NODE_STATUS_PAYLOAD_BYTES]) {
    if (payload == NULL || out_bytes == NULL) {
        return false;
    }

    out_bytes[0] = payload->soc_pct;
    write_u16_le(payload->pack_voltage_mv, &out_bytes[1]);
    write_i16_le(payload->pack_current_da, &out_bytes[3]);
    out_bytes[5] = (uint8_t)payload->temperature_c;
    out_bytes[6] = payload->status_flags;
    out_bytes[7] = payload->fault_flags;
    return true;
}

bool cluster_decode_status_payload(const uint8_t in_bytes[NODE_STATUS_PAYLOAD_BYTES],
                                   node_status_payload_t *payload) {
    if (payload == NULL || in_bytes == NULL) {
        return false;
    }

    payload->soc_pct = in_bytes[0];
    payload->pack_voltage_mv = read_u16_le(&in_bytes[1]);
    payload->pack_current_da = read_i16_le(&in_bytes[3]);
    payload->temperature_c = (int8_t)in_bytes[5];
    payload->status_flags = in_bytes[6];
    payload->fault_flags = in_bytes[7];
    return true;
}

bool cluster_encode_command_payload(const node_command_payload_t *payload,
                                    uint8_t out_bytes[NODE_COMMAND_PAYLOAD_BYTES]) {
    if (payload == NULL || out_bytes == NULL) {
        return false;
    }

    out_bytes[0] = payload->mode;
    write_i16_le(payload->charge_setpoint_da, &out_bytes[1]);
    write_i16_le(payload->discharge_setpoint_da, &out_bytes[3]);
    out_bytes[5] = payload->command_flags;
    out_bytes[6] = payload->supervision_timeout_100ms;
    out_bytes[7] = payload->command_sequence;
    return true;
}

bool cluster_decode_command_payload(const uint8_t in_bytes[NODE_COMMAND_PAYLOAD_BYTES],
                                    node_command_payload_t *payload) {
    if (payload == NULL || in_bytes == NULL) {
        return false;
    }

    payload->mode = in_bytes[0];
    payload->charge_setpoint_da = read_i16_le(&in_bytes[1]);
    payload->discharge_setpoint_da = read_i16_le(&in_bytes[3]);
    payload->command_flags = in_bytes[5];
    payload->supervision_timeout_100ms = in_bytes[6];
    payload->command_sequence = in_bytes[7];
    return true;
}

bool cluster_encode_diagnostic_chunk(const node_diagnostic_chunk_payload_t *payload,
                                     uint8_t out_bytes[NODE_DIAGNOSTIC_CHUNK_BYTES]) {
    uint8_t index;
    if (payload == NULL || out_bytes == NULL) {
        return false;
    }

    out_bytes[0] = payload->sequence;
    out_bytes[1] = payload->part_index;
    out_bytes[2] = payload->total_parts;
    out_bytes[3] = payload->chunk_length;
    for (index = 0U; index < 4U; index += 1U) {
        out_bytes[4U + index] = payload->chunk_bytes[index];
    }
    return true;
}

bool cluster_decode_diagnostic_chunk(const uint8_t in_bytes[NODE_DIAGNOSTIC_CHUNK_BYTES],
                                     node_diagnostic_chunk_payload_t *payload) {
    uint8_t index;
    if (payload == NULL || in_bytes == NULL) {
        return false;
    }

    payload->sequence = in_bytes[0];
    payload->part_index = in_bytes[1];
    payload->total_parts = in_bytes[2];
    payload->chunk_length = in_bytes[3];
    for (index = 0U; index < 4U; index += 1U) {
        payload->chunk_bytes[index] = in_bytes[4U + index];
    }
    return true;
}
