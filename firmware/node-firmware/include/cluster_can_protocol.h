#ifndef CLUSTER_CAN_PROTOCOL_H
#define CLUSTER_CAN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define CLUSTER_CAN_BAUD_RATE 500000U
#define CLUSTER_CAN_PROTOCOL_VERSION 1U
#define NODE_STATUS_BASE_ID 0x100U
#define NODE_COMMAND_BASE_ID 0x200U
#define NODE_DIAGNOSTIC_BASE_ID 0x300U
#define NODE_STATUS_PAYLOAD_BYTES 8U
#define NODE_COMMAND_PAYLOAD_BYTES 8U
#define NODE_DIAGNOSTIC_CHUNK_BYTES 8U
#define EMS_SUPERVISION_TIMEOUT_MS 1500U

#define NODE_STATUS_FLAG_CONTACTOR_CLOSED 0x01U
#define NODE_STATUS_FLAG_READY_FOR_CONNECTION 0x02U
#define NODE_STATUS_FLAG_BALANCING_ACTIVE 0x04U
#define NODE_STATUS_FLAG_MAINTENANCE_LOCKOUT 0x08U
#define NODE_STATUS_FLAG_SERVICE_LOCKOUT 0x10U

#define NODE_COMMAND_FLAG_CONTACTOR_CLOSED 0x01U
#define NODE_COMMAND_FLAG_ALLOW_BALANCING 0x02U

typedef enum {
    NODE_MODE_IDLE = 0,
    NODE_MODE_CHARGE = 1,
    NODE_MODE_DISCHARGE = 2,
    NODE_MODE_STANDBY = 3,
    NODE_MODE_CLUSTER_SLAVE_CHARGE = 10,
    NODE_MODE_CLUSTER_SLAVE_DISCHARGE = 11,
    NODE_MODE_CLUSTER_BALANCING = 12,
    NODE_MODE_CLUSTER_ISOLATED = 13
} cluster_node_mode_t;

typedef enum {
    NODE_FAULT_FLAG_NONE = 0x00U,
    NODE_FAULT_FLAG_OVER_TEMPERATURE = 0x01U,
    NODE_FAULT_FLAG_UNDER_TEMPERATURE = 0x02U,
    NODE_FAULT_FLAG_CELL_OVER_VOLTAGE = 0x04U,
    NODE_FAULT_FLAG_CELL_UNDER_VOLTAGE = 0x08U,
    NODE_FAULT_FLAG_BMS_TRIP = 0x10U,
    NODE_FAULT_FLAG_CONTACTOR_FEEDBACK = 0x20U,
    NODE_FAULT_FLAG_COMMUNICATION_TIMEOUT = 0x40U,
    NODE_FAULT_FLAG_MAINTENANCE_LOCKOUT = 0x80U
} cluster_fault_flags_t;

typedef struct {
    uint8_t soc_pct;
    uint16_t pack_voltage_mv;
    int16_t pack_current_da;
    int8_t temperature_c;
    uint8_t status_flags;
    uint8_t fault_flags;
} node_status_payload_t;

typedef struct {
    uint8_t mode;
    int16_t charge_setpoint_da;
    int16_t discharge_setpoint_da;
    uint8_t command_flags;
    uint8_t supervision_timeout_100ms;
    uint8_t command_sequence;
} node_command_payload_t;

typedef struct {
    uint8_t sequence;
    uint8_t part_index;
    uint8_t total_parts;
    uint8_t chunk_length;
    uint8_t chunk_bytes[4];
} node_diagnostic_chunk_payload_t;

uint16_t cluster_resolve_can_id(uint16_t base_id, uint8_t node_address);
void cluster_build_status_payload(node_status_payload_t *payload,
                                  uint8_t soc_pct,
                                  uint16_t pack_voltage_mv,
                                  int16_t pack_current_da,
                                  int8_t temperature_c,
                                  uint8_t status_flags,
                                  uint8_t fault_flags);
void cluster_build_command_payload(node_command_payload_t *payload,
                                   cluster_node_mode_t mode,
                                   int16_t charge_setpoint_da,
                                   int16_t discharge_setpoint_da,
                                   uint8_t command_flags,
                                   uint8_t supervision_timeout_100ms,
                                   uint8_t command_sequence);
void cluster_build_diagnostic_chunk(node_diagnostic_chunk_payload_t *payload,
                                    uint8_t sequence,
                                    uint8_t part_index,
                                    uint8_t total_parts,
                                    uint8_t chunk_length,
                                    const uint8_t chunk_bytes[4]);

bool cluster_encode_status_payload(const node_status_payload_t *payload,
                                   uint8_t out_bytes[NODE_STATUS_PAYLOAD_BYTES]);
bool cluster_decode_status_payload(const uint8_t in_bytes[NODE_STATUS_PAYLOAD_BYTES],
                                   node_status_payload_t *payload);
bool cluster_encode_command_payload(const node_command_payload_t *payload,
                                    uint8_t out_bytes[NODE_COMMAND_PAYLOAD_BYTES]);
bool cluster_decode_command_payload(const uint8_t in_bytes[NODE_COMMAND_PAYLOAD_BYTES],
                                    node_command_payload_t *payload);
bool cluster_encode_diagnostic_chunk(const node_diagnostic_chunk_payload_t *payload,
                                     uint8_t out_bytes[NODE_DIAGNOSTIC_CHUNK_BYTES]);
bool cluster_decode_diagnostic_chunk(const uint8_t in_bytes[NODE_DIAGNOSTIC_CHUNK_BYTES],
                                     node_diagnostic_chunk_payload_t *payload);

#endif
