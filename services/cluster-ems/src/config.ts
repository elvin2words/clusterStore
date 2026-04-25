import type {
  AuthorizationRole,
  CommandType,
  DispatchStrategy
} from "@clusterstore/contracts";

export interface StartupSequencerConfig {
  voltageMatchWindowMv: number;
  prechargeTimeoutMs: number;
  contactorSettleTimeoutMs: number;
  balancingTimeoutMs: number;
  balancingMaxCurrentA: number;
  startupTimeoutMs: number;
  minNodesForDispatch: number;
}

export interface RemoteCommandPolicy {
  maxCommandTtlMs: number;
  maxChargeOverrideCurrentA: number;
  maxDischargeOverrideCurrentA: number;
  allowedRolesByType: Partial<Record<CommandType, AuthorizationRole[]>>;
}

export interface ClusterEmsConfig {
  siteId: string;
  clusterId: string;
  aggregateCapacityKwh: number;
  maxChargeCurrentPerNodeA: number;
  maxDischargeCurrentPerNodeA: number;
  equalizationWindowPct: number;
  controlLoopIntervalMs: number;
  telemetryIntervalMs: number;
  supervisionTimeoutMs: number;
  defaultDispatchStrategy: DispatchStrategy;
  startup: StartupSequencerConfig;
  remoteCommands: RemoteCommandPolicy;
}
