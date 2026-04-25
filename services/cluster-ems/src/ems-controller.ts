import type {
  ClusterAlert,
  ClusterMode,
  ClusterTelemetry,
  CommandAcknowledgement,
  DispatchAllocation,
  DispatchStrategy,
  GridInverterState,
  InverterSetpoint,
  NodeCommandFrame,
  NodeStatusFrame,
  RemoteCommand
} from "@clusterstore/contracts";
import { allocateCurrent, buildNodeCommands } from "./dispatch.ts";
import { FaultManager } from "./fault-manager.ts";
import { StartupSequencer } from "./startup-sequencer.ts";
import type { CanBusPort } from "./adapters/can-bus.ts";
import type { ClusterEmsConfig } from "./config.ts";
import type { HmiPort } from "./adapters/hmi.ts";
import type { OperationalJournalPort } from "./adapters/journal.ts";
import type { GridInverterPort } from "./adapters/modbus.ts";
import type { WatchdogPort } from "./adapters/watchdog.ts";

export interface ClockPort {
  now(): Date;
}

export interface ClusterEmsDependencies {
  config: ClusterEmsConfig;
  canBus: CanBusPort;
  inverter: GridInverterPort;
  hmi: HmiPort;
  watchdog: WatchdogPort;
  clock: ClockPort;
  journal: OperationalJournalPort;
}

function makeCommandAck(
  command: RemoteCommand,
  status: CommandAcknowledgement["status"],
  timestamp: string,
  appliedClusterMode: ClusterMode,
  reason?: string
): CommandAcknowledgement {
  return {
    commandId: command.id,
    idempotencyKey: command.idempotencyKey,
    status,
    timestamp,
    reason,
    appliedClusterMode
  };
}

export class ClusterEmsController {
  private readonly dependencies: ClusterEmsDependencies;
  private clusterMode: ClusterMode = "startup_equalization";
  private dispatchStrategy: DispatchStrategy;
  private maintenanceMode = false;
  private pendingAlerts: ClusterAlert[] = [];
  private lastTelemetry?: ClusterTelemetry;
  private cumulativeEnergyInKwh = 0;
  private cumulativeEnergyOutKwh = 0;
  private remoteOverride?:
    | {
        direction: "charge" | "discharge";
        currentA: number;
        expiresAt: string;
      }
    | undefined;
  private readonly faultManager = new FaultManager();
  private readonly startupSequencer: StartupSequencer;
  private startupCompleted = false;
  private commandSequence = 0;
  private lastAcceptedRemoteSequence = 0;
  private lastGoodAggregateSnapshot?:
    | {
        aggregateSocPct: number;
        aggregateCapacityKwh: number;
        availableEnergyKwh: number;
      }
    | undefined;

  public constructor(dependencies: ClusterEmsDependencies) {
    this.dependencies = dependencies;
    this.dispatchStrategy = dependencies.config.defaultDispatchStrategy;
    this.startupSequencer = new StartupSequencer(dependencies.config);
  }

  public async runCycle(): Promise<ClusterTelemetry> {
    const timestamp = this.dependencies.clock.now().toISOString();

    try {
      await this.dependencies.watchdog.kick();

      const [nodes, inverterState] = await Promise.all([
        this.dependencies.canBus.readStatuses(),
        this.dependencies.inverter.readState()
      ]);

      const faultState = this.faultManager.evaluate(
        this.dependencies.config.siteId,
        this.dependencies.config.clusterId,
        timestamp,
        nodes,
        this.dependencies.config.supervisionTimeoutMs
      );

      for (const nodeId of faultState.isolatedNodeIds) {
        await this.dependencies.canBus.isolateNode(nodeId);
      }

      const commandSequence = this.nextCommandSequence();
      let nodeCommands: NodeCommandFrame[] = [];
      let combinedAlerts = [...faultState.alerts];
      let combinedFaults = [...faultState.activeFaults];
      let startupStatus: ClusterTelemetry["startupStatus"];

      if (this.maintenanceMode) {
        this.clusterMode = "maintenance";
        this.startupCompleted = false;
        this.startupSequencer.reset();
        nodeCommands = nodes.map((node) => ({
          nodeAddress: node.nodeAddress,
          nodeId: node.nodeId,
          mode: "cluster_isolated",
          chargeSetpointA: 0,
          dischargeSetpointA: 0,
          contactorCommandClosed: false,
          allowBalancing: false,
          supervisionTimeoutMs: this.dependencies.config.supervisionTimeoutMs,
          commandSequence
        }));
        await this.dependencies.inverter.holdOpenBus();
      } else if (!this.startupCompleted) {
        const startupPlan = this.startupSequencer.step(
          timestamp,
          nodes,
          inverterState,
          commandSequence
        );
        this.clusterMode = startupPlan.failed
          ? "safe_shutdown"
          : "startup_equalization";
        startupStatus = startupPlan.startupStatus;
        nodeCommands = startupPlan.commands;
        combinedAlerts.push(...startupPlan.alerts);
        combinedFaults.push(...startupPlan.activeFaults);

        if (startupPlan.inverterAction === "precharge" && startupPlan.inverterPrechargeTargetV) {
          await this.dependencies.inverter.prechargeDcBus(
            startupPlan.inverterPrechargeTargetV
          );
        } else if (startupPlan.inverterAction === "hold_open") {
          await this.dependencies.inverter.holdOpenBus();
        }

        if (startupPlan.ready) {
          this.startupCompleted = true;
          this.clusterMode = faultState.clusterDegraded ? "degraded" : "normal_dispatch";
        }
      } else {
        this.clusterMode = faultState.clusterDegraded ? "degraded" : "normal_dispatch";
        const dispatchAllocations = this.planDispatch(nodes, inverterState);
        nodeCommands = buildNodeCommands(
          nodes,
          dispatchAllocations,
          this.dependencies.config.supervisionTimeoutMs,
          commandSequence
        );
        await this.dependencies.inverter.writeSetpoint(
          this.toInverterSetpoint(dispatchAllocations)
        );
      }

      await this.dependencies.canBus.writeCommands(nodeCommands);
      await this.recordAlerts(combinedAlerts);

      const telemetry = this.buildTelemetry(
        timestamp,
        nodes,
        inverterState,
        combinedFaults,
        startupStatus
      );

      this.pendingAlerts.push(...combinedAlerts);
      this.lastTelemetry = telemetry;

      await this.dependencies.hmi.render(telemetry, combinedAlerts);
      return telemetry;
    } catch (error) {
      const message =
        error instanceof Error ? error.message : "Unknown EMS runtime failure.";
      await this.dependencies.watchdog.triggerFailSafe(message);
      await this.dependencies.journal.record({
        siteId: this.dependencies.config.siteId,
        clusterId: this.dependencies.config.clusterId,
        timestamp,
        kind: "ems.fail_safe",
        severity: "critical",
        message,
        metadata: {
          clusterMode: this.clusterMode
        }
      });
      throw error;
    }
  }

  public async getSnapshot(): Promise<ClusterTelemetry> {
    if (!this.lastTelemetry) {
      return this.runCycle();
    }

    return this.lastTelemetry;
  }

  public async drainAlerts(): Promise<ClusterAlert[]> {
    const alerts = [...this.pendingAlerts];
    this.pendingAlerts = [];
    return alerts;
  }

  public async applyRemoteCommand(
    command: RemoteCommand
  ): Promise<CommandAcknowledgement> {
    const timestamp = this.dependencies.clock.now().toISOString();
    const issues = this.validateRemoteCommand(command);
    if (issues.length > 0) {
      return makeCommandAck(
        command,
        "rejected",
        timestamp,
        this.clusterMode,
        issues.join(" ")
      );
    }

    this.lastAcceptedRemoteSequence = command.sequence;

    switch (command.type) {
      case "set_dispatch_mode": {
        const nextMode = command.payload.dispatchStrategy;
        if (
          nextMode === "equal_current" ||
          nextMode === "soc_weighted" ||
          nextMode === "temperature_weighted"
        ) {
          this.dispatchStrategy = nextMode;
        }
        break;
      }
      case "set_maintenance_mode": {
        this.maintenanceMode = Boolean(command.payload.enabled);
        if (this.maintenanceMode) {
          this.remoteOverride = undefined;
        }
        break;
      }
      case "clear_fault_latch": {
        if (this.faultManager.activeIncidentCount() > 0) {
          return makeCommandAck(
            command,
            "rejected",
            timestamp,
            this.clusterMode,
            "Cannot clear latched faults while active incidents remain."
          );
        }
        break;
      }
      case "force_charge":
      case "force_discharge": {
        const currentA = Number(command.payload.currentA ?? 0);
        this.dispatchStrategy = "equal_current";
        this.remoteOverride = {
          direction: command.type === "force_charge" ? "charge" : "discharge",
          currentA,
          expiresAt: command.expiresAt
        };
        break;
      }
    }

    await this.dependencies.journal.record({
      siteId: this.dependencies.config.siteId,
      clusterId: this.dependencies.config.clusterId,
      timestamp,
      kind: "command.applied",
      severity: "info",
      message: `Applied remote command ${command.type}.`,
      metadata: {
        commandId: command.id,
        requestedBy: command.requestedBy,
        role: command.authorization.role,
        sequence: command.sequence
      }
    });

    return makeCommandAck(command, "completed", timestamp, this.clusterMode);
  }

  private validateRemoteCommand(command: RemoteCommand): string[] {
    const issues: string[] = [];
    const nowMs = this.dependencies.clock.now().getTime();
    const createdAtMs = Date.parse(command.createdAt);
    const expiresAtMs = Date.parse(command.expiresAt);
    const authorizationIssuedAtMs = Date.parse(command.authorization.issuedAt);
    const authorizationExpiresAtMs = Date.parse(command.authorization.expiresAt);
    const ttlMs = expiresAtMs - nowMs;
    const allowedRoles =
      this.dependencies.config.remoteCommands.allowedRolesByType[command.type] ?? [];

    if (command.target.siteId !== this.dependencies.config.siteId) {
      issues.push("Command target site does not match this controller.");
    }

    if (command.target.clusterId !== this.dependencies.config.clusterId) {
      issues.push("Command target cluster does not match this controller.");
    }

    if ((command.target.nodeIds?.length ?? 0) > 0) {
      issues.push("Per-node targeting is not yet supported by this controller.");
    }

    if (command.sequence <= this.lastAcceptedRemoteSequence) {
      issues.push("Command sequence is stale or out of order.");
    }

    if (!Number.isFinite(createdAtMs)) {
      issues.push("Command createdAt must be a valid timestamp.");
    }

    if (!Number.isFinite(expiresAtMs) || ttlMs <= 0) {
      issues.push("Command expiry must be in the future.");
    }

    if (ttlMs > this.dependencies.config.remoteCommands.maxCommandTtlMs) {
      issues.push("Command expiry exceeds the maximum allowed TTL.");
    }

    if (
      Number.isFinite(createdAtMs) &&
      Number.isFinite(expiresAtMs) &&
      createdAtMs >= expiresAtMs
    ) {
      issues.push("Command createdAt must be before expiresAt.");
    }

    if (!allowedRoles.includes(command.authorization.role)) {
      issues.push("Command role is not permitted for this action.");
    }

    if (!Number.isFinite(authorizationIssuedAtMs)) {
      issues.push("Command authorization issuedAt must be a valid timestamp.");
    } else if (authorizationIssuedAtMs > nowMs) {
      issues.push("Command authorization issuedAt cannot be in the future.");
    }

    if (!Number.isFinite(authorizationExpiresAtMs)) {
      issues.push("Command authorization expiresAt must be a valid timestamp.");
    } else if (authorizationExpiresAtMs <= nowMs) {
      issues.push("Command authorization has expired.");
    }

    if (
      Number.isFinite(authorizationIssuedAtMs) &&
      Number.isFinite(authorizationExpiresAtMs) &&
      authorizationIssuedAtMs >= authorizationExpiresAtMs
    ) {
      issues.push("Command authorization issuedAt must be before expiresAt.");
    }

    if (!command.authorization.scopes.includes(`cluster:${command.type}`)) {
      issues.push("Command authorization scope is missing.");
    }

    if (!this.startupCompleted && command.type !== "set_maintenance_mode") {
      issues.push("Remote commands are blocked until startup equalization is complete.");
    }

    if (this.clusterMode === "safe_shutdown" && command.type !== "set_maintenance_mode") {
      issues.push("Cluster is in safe shutdown and will not accept dispatch commands.");
    }

    if (command.type === "force_charge") {
      const currentA = Number(command.payload.currentA ?? 0);
      if (
        currentA <= 0 ||
        currentA > this.dependencies.config.remoteCommands.maxChargeOverrideCurrentA
      ) {
        issues.push("Force charge current exceeds the configured safe override window.");
      }
    }

    if (command.type === "force_discharge") {
      const currentA = Number(command.payload.currentA ?? 0);
      if (
        currentA <= 0 ||
        currentA >
          this.dependencies.config.remoteCommands.maxDischargeOverrideCurrentA
      ) {
        issues.push(
          "Force discharge current exceeds the configured safe override window."
        );
      }
    }

    return issues;
  }

  private planDispatch(
    nodes: NodeStatusFrame[],
    inverterState: GridInverterState
  ): DispatchAllocation[] {
    if (this.clusterMode === "maintenance" || this.clusterMode === "safe_shutdown") {
      return [];
    }

    const remoteOverride = this.activeRemoteOverride();
    if (remoteOverride) {
      return allocateCurrent({
        strategy: "equal_current",
        direction: remoteOverride.direction,
        availableCurrentA: remoteOverride.currentA,
        nodes,
        maxCurrentPerNodeA:
          remoteOverride.direction === "charge"
            ? this.dependencies.config.maxChargeCurrentPerNodeA
            : this.dependencies.config.maxDischargeCurrentPerNodeA,
        supervisionTimeoutMs: this.dependencies.config.supervisionTimeoutMs
      });
    }

    if (inverterState.availableChargeCurrentA > 0 && inverterState.tariffBand !== "expensive") {
      return allocateCurrent({
        strategy: this.dispatchStrategy,
        direction: "charge",
        availableCurrentA: inverterState.availableChargeCurrentA,
        nodes,
        maxCurrentPerNodeA: this.dependencies.config.maxChargeCurrentPerNodeA,
        supervisionTimeoutMs: this.dependencies.config.supervisionTimeoutMs
      });
    }

    if (
      inverterState.requestedDischargeCurrentA > 0 ||
      inverterState.tariffBand === "expensive" ||
      !inverterState.gridAvailable
    ) {
      return allocateCurrent({
        strategy: this.dispatchStrategy,
        direction: "discharge",
        availableCurrentA: Math.max(
          inverterState.requestedDischargeCurrentA,
          inverterState.gridAvailable
            ? 0
            : this.dependencies.config.maxDischargeCurrentPerNodeA * nodes.length
        ),
        nodes,
        maxCurrentPerNodeA: this.dependencies.config.maxDischargeCurrentPerNodeA,
        supervisionTimeoutMs: this.dependencies.config.supervisionTimeoutMs
      });
    }

    return [];
  }

  private toInverterSetpoint(allocations: DispatchAllocation[]): InverterSetpoint {
    const aggregateChargeCurrentA = allocations.reduce(
      (sum, allocation) => sum + allocation.chargeCurrentA,
      0
    );
    const aggregateDischargeCurrentA = allocations.reduce(
      (sum, allocation) => sum + allocation.dischargeCurrentA,
      0
    );

    return {
      operatingMode:
        aggregateChargeCurrentA > 0
          ? "charge"
          : aggregateDischargeCurrentA > 0
            ? "discharge"
            : "idle",
      aggregateChargeCurrentA,
      aggregateDischargeCurrentA,
      exportLimitW: 0
    };
  }

  private buildTelemetry(
    timestamp: string,
    nodes: NodeStatusFrame[],
    inverterState: GridInverterState,
    activeFaults: ClusterTelemetry["activeFaults"],
    startupStatus?: ClusterTelemetry["startupStatus"]
  ): ClusterTelemetry {
    const freshNodes = nodes.filter(
      (node) => node.heartbeatAgeMs < this.dependencies.config.supervisionTimeoutMs
    );
    const staleNodes = nodes.filter(
      (node) => node.heartbeatAgeMs >= this.dependencies.config.supervisionTimeoutMs
    );
    const nodesForSoc = freshNodes.filter(
      (node) => !node.maintenanceLockout && !node.serviceLockout
    );

    let aggregateCapacityKwh: number;
    let availableEnergyKwh: number;
    let aggregateSocPct: number;
    let socComputation: ClusterTelemetry["socComputation"];

    if (nodesForSoc.length > 0) {
      aggregateCapacityKwh = nodesForSoc.reduce(
        (sum, node) => sum + node.ratedCapacityKwh,
        0
      );
      availableEnergyKwh = nodesForSoc.reduce(
        (sum, node) => sum + (node.ratedCapacityKwh * node.socPct) / 100,
        0
      );
      aggregateSocPct =
        aggregateCapacityKwh <= 0 ? 0 : (availableEnergyKwh / aggregateCapacityKwh) * 100;
      socComputation = "fresh_nodes";
      this.lastGoodAggregateSnapshot = {
        aggregateSocPct,
        aggregateCapacityKwh,
        availableEnergyKwh
      };
    } else if (this.lastGoodAggregateSnapshot) {
      aggregateSocPct = this.lastGoodAggregateSnapshot.aggregateSocPct;
      aggregateCapacityKwh = this.lastGoodAggregateSnapshot.aggregateCapacityKwh;
      availableEnergyKwh = this.lastGoodAggregateSnapshot.availableEnergyKwh;
      socComputation = "last_good_snapshot";
    } else {
      aggregateCapacityKwh = this.dependencies.config.aggregateCapacityKwh;
      availableEnergyKwh = 0;
      aggregateSocPct = 0;
      socComputation = "configured_capacity_fallback";
    }

    const nodeEstimatedPowerW = freshNodes.reduce(
      (sum, node) => sum + (node.packCurrentMa / 1_000) * (node.packVoltageMv / 1_000),
      0
    );
    const authoritativePowerW =
      inverterState.meteredSitePowerW ?? nodeEstimatedPowerW;
    const chargePowerW = Math.max(0, authoritativePowerW);
    const dischargePowerW = Math.max(0, authoritativePowerW * -1);
    const powerMismatchW =
      inverterState.meteredSitePowerW === undefined
        ? undefined
        : Math.abs(inverterState.meteredSitePowerW - nodeEstimatedPowerW);
    const powerMismatchToleranceW =
      inverterState.meteredSitePowerW === undefined
        ? Number.POSITIVE_INFINITY
        : Math.max(500, Math.abs(inverterState.meteredSitePowerW) * 0.1);
    const hasPowerMismatch =
      powerMismatchW !== undefined && powerMismatchW > powerMismatchToleranceW;

    const hoursPerCycle = this.dependencies.config.controlLoopIntervalMs / 3_600_000;
    this.cumulativeEnergyInKwh += (chargePowerW / 1_000) * hoursPerCycle;
    this.cumulativeEnergyOutKwh += (dischargePowerW / 1_000) * hoursPerCycle;

    const dataQuality =
      staleNodes.length > 0
        ? "stale"
        : activeFaults.length > 0 || hasPowerMismatch
          ? "degraded"
          : "good";

    return {
      siteId: this.dependencies.config.siteId,
      clusterId: this.dependencies.config.clusterId,
      timestamp,
      clusterMode: this.clusterMode,
      dispatchStrategy: this.dispatchStrategy,
      aggregateSocPct,
      aggregateCapacityKwh,
      availableEnergyKwh,
      chargePowerW,
      dischargePowerW,
      cumulativeEnergyInKwh: Number(this.cumulativeEnergyInKwh.toFixed(3)),
      cumulativeEnergyOutKwh: Number(this.cumulativeEnergyOutKwh.toFixed(3)),
      dataQuality,
      socComputation,
      powerComputation:
        inverterState.meteredSitePowerW === undefined ? "node_estimate" : "site_meter",
      freshNodeCount: freshNodes.length,
      staleNodeCount: staleNodes.length,
      nodeEstimatedPowerW: Number(nodeEstimatedPowerW.toFixed(3)),
      meteredSitePowerW: inverterState.meteredSitePowerW,
      powerMismatchW:
        powerMismatchW === undefined ? undefined : Number(powerMismatchW.toFixed(3)),
      incidentCount: activeFaults.length,
      startupStatus,
      nodeSocBreakdown: nodes.map((node) => ({
        nodeId: node.nodeId,
        nodeAddress: node.nodeAddress,
        capacityKwh: node.ratedCapacityKwh,
        socPct: node.socPct,
        temperatureDeciC: node.temperatureDeciC,
        contactorClosed: node.contactorClosed,
        readyForConnection: node.readyForConnection,
        serviceLockout: node.serviceLockout,
        faultFlags: node.faultFlags
      })),
      acOutputVoltageV: inverterState.acOutputVoltageV,
      acOutputFrequencyHz: inverterState.acOutputFrequencyHz,
      acOutputLoadW: inverterState.acOutputLoadW,
      solarGenerationW: inverterState.solarGenerationW,
      activeFaults
    };
  }

  private activeRemoteOverride():
    | {
        direction: "charge" | "discharge";
        currentA: number;
      }
    | undefined {
    if (!this.remoteOverride) {
      return undefined;
    }

    const now = this.dependencies.clock.now().getTime();
    const expiry = Date.parse(this.remoteOverride.expiresAt);
    if (Number.isNaN(expiry) || expiry <= now) {
      this.remoteOverride = undefined;
      return undefined;
    }

    return this.remoteOverride;
  }

  private async recordAlerts(alerts: ClusterAlert[]): Promise<void> {
    for (const alert of alerts) {
      await this.dependencies.journal.record({
        siteId: this.dependencies.config.siteId,
        clusterId: this.dependencies.config.clusterId,
        timestamp: alert.timestamp,
        kind: `alert.${alert.state}`,
        severity: alert.severity,
        message: alert.message,
        metadata: {
          code: alert.code,
          nodeId: alert.nodeId,
          incidentKey: alert.incidentKey
        }
      });
    }
  }

  private nextCommandSequence(): number {
    this.commandSequence = (this.commandSequence + 1) % 256;
    return this.commandSequence;
  }
}
