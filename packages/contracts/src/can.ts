import type {
  FaultCode,
  NodeCommandFrame,
  NodeDiagnosticFrame,
  NodeMode,
  NodeStatusFrame
} from "./types.ts";

export const CAN_PROTOCOL_VERSION = 1;
export const CAN_BAUD_RATE = 500_000;
export const NODE_SUPERVISION_TIMEOUT_MS = 1_500;
export const NODE_STATUS_PAYLOAD_BYTES = 8;
export const NODE_COMMAND_PAYLOAD_BYTES = 8;
export const NODE_DIAGNOSTIC_CHUNK_BYTES = 8;

export const NODE_STATUS_FLAG_CONTACTOR_CLOSED = 1 << 0;
export const NODE_STATUS_FLAG_READY_FOR_CONNECTION = 1 << 1;
export const NODE_STATUS_FLAG_BALANCING_ACTIVE = 1 << 2;
export const NODE_STATUS_FLAG_MAINTENANCE_LOCKOUT = 1 << 3;
export const NODE_STATUS_FLAG_SERVICE_LOCKOUT = 1 << 4;

export const NODE_COMMAND_FLAG_CONTACTOR_CLOSED = 1 << 0;
export const NODE_COMMAND_FLAG_ALLOW_BALANCING = 1 << 1;

export const NODE_FAULT_FLAG_OVER_TEMPERATURE = 1 << 0;
export const NODE_FAULT_FLAG_UNDER_TEMPERATURE = 1 << 1;
export const NODE_FAULT_FLAG_CELL_OVER_VOLTAGE = 1 << 2;
export const NODE_FAULT_FLAG_CELL_UNDER_VOLTAGE = 1 << 3;
export const NODE_FAULT_FLAG_BMS_TRIP = 1 << 4;
export const NODE_FAULT_FLAG_CONTACTOR_FEEDBACK = 1 << 5;
export const NODE_FAULT_FLAG_COMMUNICATION_TIMEOUT = 1 << 6;
export const NODE_FAULT_FLAG_MAINTENANCE_LOCKOUT = 1 << 7;

const NODE_MODE_TO_WIRE: Record<NodeMode, number> = {
  idle: 0,
  charge: 1,
  discharge: 2,
  standby: 3,
  cluster_slave_charge: 10,
  cluster_slave_discharge: 11,
  cluster_balancing: 12,
  cluster_isolated: 13
};

const WIRE_TO_NODE_MODE = new Map<number, NodeMode>(
  Object.entries(NODE_MODE_TO_WIRE).map(([mode, code]) => [code, mode as NodeMode])
);

const NODE_WIRE_FAULTS: Array<{ bit: number; code: FaultCode }> = [
  { bit: NODE_FAULT_FLAG_OVER_TEMPERATURE, code: "over_temperature" },
  { bit: NODE_FAULT_FLAG_UNDER_TEMPERATURE, code: "under_temperature" },
  { bit: NODE_FAULT_FLAG_CELL_OVER_VOLTAGE, code: "cell_over_voltage" },
  { bit: NODE_FAULT_FLAG_CELL_UNDER_VOLTAGE, code: "cell_under_voltage" },
  { bit: NODE_FAULT_FLAG_BMS_TRIP, code: "bms_trip" },
  { bit: NODE_FAULT_FLAG_CONTACTOR_FEEDBACK, code: "contactor_feedback_fault" },
  { bit: NODE_FAULT_FLAG_COMMUNICATION_TIMEOUT, code: "communication_timeout" },
  { bit: NODE_FAULT_FLAG_MAINTENANCE_LOCKOUT, code: "maintenance_lockout" }
];

export interface CanNodeStatusPayload {
  socPct: number;
  packVoltageMv: number;
  packCurrentDa: number;
  temperatureC: number;
  statusFlags: number;
  faultFlags: number;
}

export interface CanNodeCommandPayload {
  modeCode: number;
  chargeSetpointDa: number;
  dischargeSetpointDa: number;
  commandFlags: number;
  supervisionTimeoutTicks100Ms: number;
  commandSequence: number;
}

export interface CanNodeDiagnosticChunkPayload {
  sequence: number;
  partIndex: number;
  totalParts: number;
  chunkLength: number;
  chunkBytes: [number, number, number, number];
}

export interface CanFrameDefinition<TPayload> {
  key: string;
  baseId: number;
  cadenceMs: number;
  transport: "classic-can" | "segmented" | "can-fd";
  payloadBytes: number;
  payloadExample: TPayload;
}

export const CAN_FRAME_SCHEMA: {
  nodeStatus: CanFrameDefinition<CanNodeStatusPayload>;
  nodeCommand: CanFrameDefinition<CanNodeCommandPayload>;
  nodeDiagnostic: CanFrameDefinition<CanNodeDiagnosticChunkPayload>;
} = {
  nodeStatus: {
    key: "NODE_STATUS",
    baseId: 0x100,
    cadenceMs: 100,
    transport: "classic-can",
    payloadBytes: NODE_STATUS_PAYLOAD_BYTES,
    payloadExample: {
      socPct: 74,
      packVoltageMv: 51520,
      packCurrentDa: -82,
      temperatureC: 29,
      statusFlags:
        NODE_STATUS_FLAG_CONTACTOR_CLOSED |
        NODE_STATUS_FLAG_READY_FOR_CONNECTION,
      faultFlags: 0
    }
  },
  nodeCommand: {
    key: "NODE_CMD",
    baseId: 0x200,
    cadenceMs: 100,
    transport: "classic-can",
    payloadBytes: NODE_COMMAND_PAYLOAD_BYTES,
    payloadExample: {
      modeCode: NODE_MODE_TO_WIRE.cluster_slave_discharge,
      chargeSetpointDa: 0,
      dischargeSetpointDa: 120,
      commandFlags:
        NODE_COMMAND_FLAG_CONTACTOR_CLOSED |
        NODE_COMMAND_FLAG_ALLOW_BALANCING,
      supervisionTimeoutTicks100Ms: NODE_SUPERVISION_TIMEOUT_MS / 100,
      commandSequence: 7
    }
  },
  nodeDiagnostic: {
    key: "NODE_DIAG",
    baseId: 0x300,
    cadenceMs: 1_000,
    transport: "segmented",
    payloadBytes: NODE_DIAGNOSTIC_CHUNK_BYTES,
    payloadExample: {
      sequence: 12,
      partIndex: 0,
      totalParts: 3,
      chunkLength: 4,
      chunkBytes: [0x10, 0x22, 0x33, 0x44]
    }
  }
};

export function resolveCanId(baseId: number, nodeAddress: number): number {
  return baseId + nodeAddress;
}

export function nodeIdFromAddress(nodeAddress: number): string {
  return `node-${String(nodeAddress).padStart(2, "0")}`;
}

export function nodeFaultMaskFromFaults(faults: FaultCode[]): number {
  return NODE_WIRE_FAULTS.reduce((mask, definition) => {
    return faults.includes(definition.code) ? mask | definition.bit : mask;
  }, 0);
}

export function faultsFromNodeFaultMask(mask: number): FaultCode[] {
  return NODE_WIRE_FAULTS.filter((definition) => (mask & definition.bit) !== 0).map(
    (definition) => definition.code
  );
}

function assertPayloadLength(bytes: Uint8Array, expected: number, frameName: string): void {
  if (bytes.byteLength !== expected) {
    throw new Error(`${frameName} payload must be ${expected} bytes.`);
  }
}

export function encodeNodeStatusPayload(payload: CanNodeStatusPayload): Uint8Array {
  const bytes = new Uint8Array(NODE_STATUS_PAYLOAD_BYTES);
  const view = new DataView(bytes.buffer);
  bytes[0] = payload.socPct & 0xff;
  view.setUint16(1, payload.packVoltageMv, true);
  view.setInt16(3, payload.packCurrentDa, true);
  view.setInt8(5, payload.temperatureC);
  bytes[6] = payload.statusFlags & 0xff;
  bytes[7] = payload.faultFlags & 0xff;
  return bytes;
}

export function decodeNodeStatusPayload(bytes: Uint8Array): CanNodeStatusPayload {
  assertPayloadLength(bytes, NODE_STATUS_PAYLOAD_BYTES, "NODE_STATUS");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return {
    socPct: bytes[0],
    packVoltageMv: view.getUint16(1, true),
    packCurrentDa: view.getInt16(3, true),
    temperatureC: view.getInt8(5),
    statusFlags: bytes[6],
    faultFlags: bytes[7]
  };
}

export function encodeNodeCommandPayload(command: NodeCommandFrame): Uint8Array {
  const bytes = new Uint8Array(NODE_COMMAND_PAYLOAD_BYTES);
  const view = new DataView(bytes.buffer);
  const modeCode = NODE_MODE_TO_WIRE[command.mode];
  const commandFlags =
    (command.contactorCommandClosed ? NODE_COMMAND_FLAG_CONTACTOR_CLOSED : 0) |
    (command.allowBalancing ? NODE_COMMAND_FLAG_ALLOW_BALANCING : 0);

  bytes[0] = modeCode & 0xff;
  view.setInt16(1, Math.round(command.chargeSetpointA * 10), true);
  view.setInt16(3, Math.round(command.dischargeSetpointA * 10), true);
  bytes[5] = commandFlags & 0xff;
  bytes[6] = Math.max(1, Math.min(255, Math.round(command.supervisionTimeoutMs / 100)));
  bytes[7] = command.commandSequence & 0xff;
  return bytes;
}

export function decodeNodeCommandPayload(
  nodeAddress: number,
  bytes: Uint8Array
): NodeCommandFrame {
  assertPayloadLength(bytes, NODE_COMMAND_PAYLOAD_BYTES, "NODE_CMD");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const commandFlags = bytes[5];
  return {
    nodeAddress,
    nodeId: nodeIdFromAddress(nodeAddress),
    mode: WIRE_TO_NODE_MODE.get(bytes[0]) ?? "standby",
    chargeSetpointA: view.getInt16(1, true) / 10,
    dischargeSetpointA: view.getInt16(3, true) / 10,
    contactorCommandClosed:
      (commandFlags & NODE_COMMAND_FLAG_CONTACTOR_CLOSED) !== 0,
    allowBalancing:
      (commandFlags & NODE_COMMAND_FLAG_ALLOW_BALANCING) !== 0,
    supervisionTimeoutMs: bytes[6] * 100,
    commandSequence: bytes[7]
  };
}

export function encodeNodeDiagnosticChunkPayload(
  payload: CanNodeDiagnosticChunkPayload
): Uint8Array {
  const bytes = new Uint8Array(NODE_DIAGNOSTIC_CHUNK_BYTES);
  bytes[0] = payload.sequence & 0xff;
  bytes[1] = payload.partIndex & 0xff;
  bytes[2] = payload.totalParts & 0xff;
  bytes[3] = payload.chunkLength & 0xff;
  bytes.set(payload.chunkBytes, 4);
  return bytes;
}

export function decodeNodeDiagnosticChunkPayload(
  bytes: Uint8Array
): CanNodeDiagnosticChunkPayload {
  assertPayloadLength(bytes, NODE_DIAGNOSTIC_CHUNK_BYTES, "NODE_DIAG");
  return {
    sequence: bytes[0],
    partIndex: bytes[1],
    totalParts: bytes[2],
    chunkLength: bytes[3],
    chunkBytes: [bytes[4], bytes[5], bytes[6], bytes[7]]
  };
}

export function toNodeStatusFrame(
  nodeAddress: number,
  payload: CanNodeStatusPayload,
  metadata: {
    ratedCapacityKwh: number;
    heartbeatAgeMs: number;
    nodeId?: string;
  }
): NodeStatusFrame {
  return {
    nodeAddress,
    nodeId: metadata.nodeId ?? nodeIdFromAddress(nodeAddress),
    ratedCapacityKwh: metadata.ratedCapacityKwh,
    socPct: payload.socPct,
    packVoltageMv: payload.packVoltageMv,
    packCurrentMa: payload.packCurrentDa * 100,
    temperatureDeciC: payload.temperatureC * 10,
    faultFlags: faultsFromNodeFaultMask(payload.faultFlags),
    contactorClosed:
      (payload.statusFlags & NODE_STATUS_FLAG_CONTACTOR_CLOSED) !== 0,
    readyForConnection:
      (payload.statusFlags & NODE_STATUS_FLAG_READY_FOR_CONNECTION) !== 0,
    balancingActive:
      (payload.statusFlags & NODE_STATUS_FLAG_BALANCING_ACTIVE) !== 0,
    maintenanceLockout:
      (payload.statusFlags & NODE_STATUS_FLAG_MAINTENANCE_LOCKOUT) !== 0,
    serviceLockout:
      (payload.statusFlags & NODE_STATUS_FLAG_SERVICE_LOCKOUT) !== 0,
    heartbeatAgeMs: metadata.heartbeatAgeMs
  };
}

export function toNodeStatusPayload(frame: NodeStatusFrame): CanNodeStatusPayload {
  const statusFlags =
    (frame.contactorClosed ? NODE_STATUS_FLAG_CONTACTOR_CLOSED : 0) |
    (frame.readyForConnection ? NODE_STATUS_FLAG_READY_FOR_CONNECTION : 0) |
    (frame.balancingActive ? NODE_STATUS_FLAG_BALANCING_ACTIVE : 0) |
    (frame.maintenanceLockout ? NODE_STATUS_FLAG_MAINTENANCE_LOCKOUT : 0) |
    (frame.serviceLockout ? NODE_STATUS_FLAG_SERVICE_LOCKOUT : 0);

  return {
    socPct: Math.round(frame.socPct),
    packVoltageMv: Math.round(frame.packVoltageMv),
    packCurrentDa: Math.round(frame.packCurrentMa / 100),
    temperatureC: Math.round(frame.temperatureDeciC / 10),
    statusFlags,
    faultFlags: nodeFaultMaskFromFaults(frame.faultFlags)
  };
}

export function toDiagnosticChunkFrames(
  nodeAddress: number,
  diagnostic: NodeDiagnosticFrame,
  sequence: number
): Array<{ canId: number; payload: Uint8Array }> {
  const serialized = new TextEncoder().encode(JSON.stringify(diagnostic));
  const totalParts = Math.ceil(serialized.length / 4);
  const frames: Array<{ canId: number; payload: Uint8Array }> = [];

  for (let partIndex = 0; partIndex < totalParts; partIndex += 1) {
    const slice = serialized.slice(partIndex * 4, partIndex * 4 + 4);
    const chunkBytes: [number, number, number, number] = [
      slice[0] ?? 0,
      slice[1] ?? 0,
      slice[2] ?? 0,
      slice[3] ?? 0
    ];
    frames.push({
      canId: resolveCanId(CAN_FRAME_SCHEMA.nodeDiagnostic.baseId, nodeAddress),
      payload: encodeNodeDiagnosticChunkPayload({
        sequence,
        partIndex,
        totalParts,
        chunkLength: slice.length,
        chunkBytes
      })
    });
  }

  return frames;
}
