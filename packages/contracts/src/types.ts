export type ClusterMode =
  | "startup_equalization"
  | "normal_dispatch"
  | "degraded"
  | "maintenance"
  | "safe_shutdown";

export type StartupPhase =
  | "discover_nodes"
  | "precharge_primary"
  | "close_primary"
  | "admit_nodes"
  | "balance_cluster"
  | "ready"
  | "failed";

export type NodeMode =
  | "idle"
  | "charge"
  | "discharge"
  | "standby"
  | "cluster_slave_charge"
  | "cluster_slave_discharge"
  | "cluster_balancing"
  | "cluster_isolated";

export type DispatchStrategy =
  | "equal_current"
  | "soc_weighted"
  | "temperature_weighted";

export type CommandType =
  | "force_charge"
  | "force_discharge"
  | "set_dispatch_mode"
  | "set_maintenance_mode"
  | "clear_fault_latch";

export type AuthorizationRole =
  | "operator"
  | "technician"
  | "service"
  | "fleet_controller";

export type AlertSeverity = "info" | "warning" | "critical";
export type AlertState = "opened" | "acknowledged" | "cleared";
export type DataQuality = "good" | "degraded" | "stale";
export type SocComputation = "fresh_nodes" | "last_good_snapshot" | "configured_capacity_fallback";
export type PowerComputation = "site_meter" | "node_estimate";

export type TariffBand = "cheap" | "normal" | "expensive" | "unavailable";

export type FaultCode =
  | "over_temperature"
  | "under_temperature"
  | "cell_over_voltage"
  | "cell_under_voltage"
  | "bms_trip"
  | "dc_bus_fault"
  | "contactor_feedback_fault"
  | "communication_timeout"
  | "modbus_fault"
  | "watchdog_fault"
  | "lte_fault"
  | "maintenance_lockout"
  | "startup_sequence_fault"
  | "voltage_mismatch"
  | "command_rejected"
  | "telemetry_buffer_fault";

export interface NodeStatusFrame {
  nodeAddress: number;
  nodeId: string;
  ratedCapacityKwh: number;
  socPct: number;
  packVoltageMv: number;
  packCurrentMa: number;
  temperatureDeciC: number;
  faultFlags: FaultCode[];
  contactorClosed: boolean;
  readyForConnection: boolean;
  balancingActive: boolean;
  maintenanceLockout: boolean;
  serviceLockout: boolean;
  heartbeatAgeMs: number;
}

export interface NodeDiagnosticFrame {
  nodeAddress: number;
  nodeId: string;
  cellVoltagesMv: number[];
  cycleCount: number;
  cumulativeThroughputKwh: number;
  balancingActive: boolean;
  firmwareVersion: string;
}

export interface NodeCommandFrame {
  nodeAddress: number;
  nodeId: string;
  mode: NodeMode;
  chargeSetpointA: number;
  dischargeSetpointA: number;
  contactorCommandClosed: boolean;
  allowBalancing: boolean;
  supervisionTimeoutMs: number;
  commandSequence: number;
}

export interface GridInverterState {
  acInputVoltageV: number;
  acInputFrequencyHz: number;
  acOutputVoltageV: number;
  acOutputFrequencyHz: number;
  acOutputLoadW: number;
  dcBusVoltageV: number;
  gridAvailable: boolean;
  solarGenerationW: number;
  availableChargeCurrentA: number;
  requestedDischargeCurrentA: number;
  exportAllowed: boolean;
  tariffBand: TariffBand;
  meteredSitePowerW?: number;
}

export interface InverterSetpoint {
  operatingMode: "idle" | "charge" | "discharge" | "grid_support";
  aggregateChargeCurrentA: number;
  aggregateDischargeCurrentA: number;
  exportLimitW: number;
}

export interface DispatchAllocation {
  nodeId: string;
  nodeAddress: number;
  chargeCurrentA: number;
  dischargeCurrentA: number;
  reason: string;
}

export interface ClusterFaultRecord {
  incidentKey: string;
  code: FaultCode;
  severity: AlertSeverity;
  source: "node" | "ems" | "bridge" | "inverter";
  nodeId?: string;
  firstObservedAt: string;
  lastObservedAt: string;
  message: string;
}

export interface StartupStatus {
  phase: StartupPhase;
  primaryNodeId?: string;
  admittedNodeIds: string[];
  blockedNodeIds: string[];
  pendingNodeIds: string[];
  reason: string;
}

export interface ClusterTelemetry {
  siteId: string;
  clusterId: string;
  timestamp: string;
  clusterMode: ClusterMode;
  dispatchStrategy: DispatchStrategy;
  aggregateSocPct: number;
  aggregateCapacityKwh: number;
  availableEnergyKwh: number;
  chargePowerW: number;
  dischargePowerW: number;
  cumulativeEnergyInKwh: number;
  cumulativeEnergyOutKwh: number;
  dataQuality: DataQuality;
  socComputation: SocComputation;
  powerComputation: PowerComputation;
  freshNodeCount: number;
  staleNodeCount: number;
  nodeEstimatedPowerW: number;
  meteredSitePowerW?: number;
  powerMismatchW?: number;
  incidentCount: number;
  startupStatus?: StartupStatus;
  nodeSocBreakdown: Array<{
    nodeId: string;
    nodeAddress: number;
    capacityKwh: number;
    socPct: number;
    temperatureDeciC: number;
    contactorClosed: boolean;
    readyForConnection: boolean;
    serviceLockout: boolean;
    faultFlags: FaultCode[];
  }>;
  acOutputVoltageV: number;
  acOutputFrequencyHz: number;
  acOutputLoadW: number;
  solarGenerationW: number;
  activeFaults: ClusterFaultRecord[];
}

export interface ClusterAlert {
  id: string;
  incidentKey: string;
  siteId: string;
  clusterId: string;
  timestamp: string;
  severity: AlertSeverity;
  state: AlertState;
  source: "node" | "ems" | "bridge" | "inverter";
  code: FaultCode;
  message: string;
  nodeId?: string;
}

export interface CommandTarget {
  siteId: string;
  clusterId: string;
  nodeIds?: string[];
}

export interface CommandAuthorization {
  tokenId: string;
  role: AuthorizationRole;
  scopes: string[];
  issuedAt: string;
  expiresAt: string;
}

export interface RemoteCommand {
  id: string;
  idempotencyKey: string;
  sequence: number;
  type: CommandType;
  createdAt: string;
  expiresAt: string;
  requestedBy: string;
  target: CommandTarget;
  authorization: CommandAuthorization;
  payload: Record<string, unknown>;
}

export interface CommandAcknowledgement {
  commandId: string;
  idempotencyKey: string;
  status: "accepted" | "rejected" | "completed" | "duplicate";
  timestamp: string;
  reason?: string;
  appliedClusterMode?: ClusterMode;
}

export interface OperationalEvent {
  siteId: string;
  clusterId: string;
  timestamp: string;
  kind: string;
  severity: AlertSeverity;
  message: string;
  metadata?: Record<string, unknown>;
}
