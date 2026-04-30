import type {
  ClusterAlert,
  ClusterFaultRecord,
  GridInverterState,
  NodeCommandFrame,
  NodeStatusFrame,
  StartupPhase,
  StartupStatus
} from "@clusterstore/contracts";
import { allocateCurrent } from "./dispatch.ts";
import type { ClusterEmsConfig } from "./config.ts";

export interface StartupPlan {
  commands: NodeCommandFrame[];
  phase: StartupPhase;
  startupStatus: StartupStatus;
  alerts: ClusterAlert[];
  activeFaults: ClusterFaultRecord[];
  ready: boolean;
  failed: boolean;
  inverterAction: "hold_open" | "precharge" | "none";
  inverterPrechargeTargetV?: number;
}

function incidentKey(code: string, nodeId?: string): string {
  return nodeId ? `${code}:${nodeId}` : code;
}

function buildAlert(
  siteId: string,
  clusterId: string,
  timestamp: string,
  code: ClusterAlert["code"],
  severity: ClusterAlert["severity"],
  message: string,
  state: ClusterAlert["state"],
  nodeId?: string
): ClusterAlert {
  const key = incidentKey(code, nodeId);
  return {
    id: `${key}:${state}:${timestamp}`,
    incidentKey: key,
    siteId,
    clusterId,
    timestamp,
    severity,
    state,
    source: "ems",
    code,
    message,
    nodeId
  };
}

function buildFault(
  timestamp: string,
  code: ClusterFaultRecord["code"],
  severity: ClusterFaultRecord["severity"],
  message: string,
  nodeId?: string
): ClusterFaultRecord {
  return {
    incidentKey: incidentKey(code, nodeId),
    code,
    severity,
    source: "ems",
    nodeId,
    firstObservedAt: timestamp,
    lastObservedAt: timestamp,
    message
  };
}

function voltageDeltaMv(node: NodeStatusFrame, inverterState: GridInverterState): number {
  return Math.abs(node.packVoltageMv - Math.round(inverterState.dcBusVoltageV * 1_000));
}

function buildCommand(
  node: NodeStatusFrame,
  supervisionTimeoutMs: number,
  commandSequence: number,
  overrides?: Partial<NodeCommandFrame>
): NodeCommandFrame {
  return {
    nodeAddress: node.nodeAddress,
    nodeId: node.nodeId,
    mode: overrides?.mode ?? "cluster_isolated",
    chargeSetpointA: overrides?.chargeSetpointA ?? 0,
    dischargeSetpointA: overrides?.dischargeSetpointA ?? 0,
    contactorCommandClosed: overrides?.contactorCommandClosed ?? false,
    allowBalancing: overrides?.allowBalancing ?? false,
    supervisionTimeoutMs,
    commandSequence: overrides?.commandSequence ?? commandSequence
  };
}

function sortCandidateNodes(nodes: NodeStatusFrame[]): NodeStatusFrame[] {
  return [...nodes].sort((left, right) => {
    if (left.socPct !== right.socPct) {
      return left.socPct - right.socPct;
    }

    return left.packVoltageMv - right.packVoltageMv;
  });
}

export class StartupSequencer {
  private readonly config: ClusterEmsConfig;
  private phase: StartupPhase = "discover_nodes";
  private startedAtMs?: number;
  private phaseEnteredAtMs?: number;
  private primaryNodeId?: string;
  private pendingNodeIds: string[] = [];
  private admittedNodeIds = new Set<string>();
  private blockedNodeIds = new Set<string>();
  private currentCandidateNodeId?: string;
  private lastFailureReason?: string;

  public constructor(config: ClusterEmsConfig) {
    this.config = config;
  }

  public reset(): void {
    this.phase = "discover_nodes";
    this.startedAtMs = undefined;
    this.phaseEnteredAtMs = undefined;
    this.primaryNodeId = undefined;
    this.pendingNodeIds = [];
    this.admittedNodeIds.clear();
    this.blockedNodeIds.clear();
    this.currentCandidateNodeId = undefined;
    this.lastFailureReason = undefined;
  }

  public isComplete(): boolean {
    return this.phase === "ready";
  }

  public step(
    timestamp: string,
    nodes: NodeStatusFrame[],
    inverterState: GridInverterState,
    commandSequence: number
  ): StartupPlan {
    const alerts: ClusterAlert[] = [];
    const syntheticFaults: ClusterFaultRecord[] = [];
    const nowMs = Date.parse(timestamp);
    const healthyNodes = sortCandidateNodes(
      nodes.filter(
        (node) =>
          node.faultFlags.length === 0 &&
          !node.maintenanceLockout &&
          !node.serviceLockout &&
          node.readyForConnection &&
          node.heartbeatAgeMs < this.config.supervisionTimeoutMs
      )
    );

    if (!this.startedAtMs) {
      this.startedAtMs = nowMs;
      this.phaseEnteredAtMs = nowMs;
    }

    if (nowMs - this.startedAtMs > this.config.startup.startupTimeoutMs) {
      return this.failedPlan(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        "Startup equalization timed out before the cluster became dispatch-ready."
      );
    }

    if (healthyNodes.length < this.config.startup.minNodesForDispatch) {
      return this.planForPhase(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        this.phase === "discover_nodes"
          ? "Waiting for enough healthy nodes to begin startup."
          : "Waiting for enough healthy nodes to continue startup.",
        "hold_open"
      );
    }

    if (this.phase === "discover_nodes") {
      const primary = healthyNodes[0];
      if (!primary) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "No healthy nodes are available for startup."
        );
      }

      this.primaryNodeId = primary.nodeId;
      this.pendingNodeIds = healthyNodes.slice(1).map((node) => node.nodeId);
      this.transition("precharge_primary", nowMs);
    }

    if (this.phase === "precharge_primary") {
      const primary = nodes.find((node) => node.nodeId === this.primaryNodeId);
      if (!primary) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "Primary startup node disappeared during precharge."
        );
      }

      if (this.phaseElapsedMs(nowMs) > this.config.startup.prechargeTimeoutMs) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "The DC bus failed to precharge to the primary node voltage window."
        );
      }

      if (
        voltageDeltaMv(primary, inverterState) <= this.config.startup.voltageMatchWindowMv
      ) {
        this.transition("close_primary", nowMs);
      }

      return this.planForPhase(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        "Precharging the DC bus to match the primary node.",
        "precharge",
        primary.packVoltageMv / 1_000
      );
    }

    if (this.phase === "close_primary") {
      const primary = nodes.find((node) => node.nodeId === this.primaryNodeId);
      if (!primary) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "Primary startup node disappeared before contactor closure."
        );
      }

      if (primary.contactorClosed) {
        this.admittedNodeIds.add(primary.nodeId);
        this.transition("admit_nodes", nowMs);
      } else if (
        this.phaseElapsedMs(nowMs) >
        this.config.startup.contactorSettleTimeoutMs
      ) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          `Primary node ${primary.nodeId} did not confirm contactor closure.`
        );
      }

      return this.planForPhase(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        "Closing the primary node contactor.",
        "none",
        undefined,
        (node) =>
          node.nodeId === primary.nodeId
            ? buildCommand(node, this.config.supervisionTimeoutMs, commandSequence, {
                mode: "standby",
                contactorCommandClosed: true
              })
            : buildCommand(node, this.config.supervisionTimeoutMs, commandSequence)
      );
    }

    if (this.phase === "admit_nodes") {
      const remainingCandidates = this.pendingNodeIds.filter(
        (nodeId) =>
          !this.admittedNodeIds.has(nodeId) && !this.blockedNodeIds.has(nodeId)
      );

      if (remainingCandidates.length === 0) {
        this.transition("balance_cluster", nowMs);
      } else {
        const candidate = nodes.find((node) => node.nodeId === remainingCandidates[0]);
        if (!candidate) {
          this.blockedNodeIds.add(remainingCandidates[0]);
        } else if (
          voltageDeltaMv(candidate, inverterState) >
          this.config.startup.voltageMatchWindowMv
        ) {
          this.blockedNodeIds.add(candidate.nodeId);
          this.currentCandidateNodeId = undefined;
          alerts.push(
            buildAlert(
              this.config.siteId,
              this.config.clusterId,
              timestamp,
              "voltage_mismatch",
              "warning",
              `Node ${candidate.nodeId} remains isolated because its pack voltage is outside the safe admission window.`,
              "opened",
              candidate.nodeId
            )
          );
          syntheticFaults.push(
            buildFault(
              timestamp,
              "voltage_mismatch",
              "warning",
              `Node ${candidate.nodeId} remains isolated because its pack voltage is outside the safe admission window.`,
              candidate.nodeId
            )
          );
        } else {
          if (this.currentCandidateNodeId !== candidate.nodeId) {
            this.currentCandidateNodeId = candidate.nodeId;
            this.phaseEnteredAtMs = nowMs;
          }

          if (candidate.contactorClosed) {
            this.admittedNodeIds.add(candidate.nodeId);
            this.currentCandidateNodeId = undefined;
          } else if (
            this.phaseElapsedMs(nowMs) >
            this.config.startup.contactorSettleTimeoutMs
          ) {
            return this.failedPlan(
              timestamp,
              nodes,
              commandSequence,
              alerts,
              syntheticFaults,
              `Candidate node ${candidate.nodeId} did not confirm contactor closure.`
            );
          }

          return this.planForPhase(
            timestamp,
            nodes,
            commandSequence,
            alerts,
            syntheticFaults,
            `Admitting ${candidate.nodeId} onto the cluster bus.`,
            "none",
            undefined,
            (node) => {
              if (
                this.admittedNodeIds.has(node.nodeId) ||
                node.nodeId === candidate.nodeId
              ) {
                return buildCommand(
                  node,
                  this.config.supervisionTimeoutMs,
                  commandSequence,
                  {
                    mode: "standby",
                    contactorCommandClosed: true
                  }
                );
              }

              return buildCommand(node, this.config.supervisionTimeoutMs, commandSequence);
            }
          );
        }
      }
    }

    if (this.phase === "balance_cluster") {
      const admittedNodes = nodes.filter((node) => this.admittedNodeIds.has(node.nodeId));
      if (admittedNodes.length < this.config.startup.minNodesForDispatch) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "Too few nodes were admitted to complete startup."
        );
      }

      const socValues = admittedNodes.map((node) => node.socPct);
      const socSpread = Math.max(...socValues) - Math.min(...socValues);
      if (socSpread <= this.config.equalizationWindowPct) {
        this.transition("ready", nowMs);
        return this.planForPhase(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "Startup equalization is complete.",
          "none",
          undefined,
          (node) =>
            this.admittedNodeIds.has(node.nodeId)
              ? buildCommand(node, this.config.supervisionTimeoutMs, commandSequence, {
                  mode: "standby",
                  contactorCommandClosed: true
                })
              : buildCommand(node, this.config.supervisionTimeoutMs, commandSequence)
        );
      } else if (
        this.phaseElapsedMs(nowMs) >
        this.config.startup.balancingTimeoutMs
      ) {
        return this.failedPlan(
          timestamp,
          nodes,
          commandSequence,
          alerts,
          syntheticFaults,
          "Connected nodes did not converge to the startup equalization window in time."
        );
      }

      const allocations = allocateCurrent({
        strategy: "soc_weighted",
        direction: "charge",
        availableCurrentA: Math.min(
          inverterState.availableChargeCurrentA,
          admittedNodes.length * this.config.startup.balancingMaxCurrentA
        ),
        nodes: admittedNodes,
        maxCurrentPerNodeA: this.config.startup.balancingMaxCurrentA,
        supervisionTimeoutMs: this.config.supervisionTimeoutMs
      });
      const allocationByNode = new Map(
        allocations.map((allocation) => [allocation.nodeId, allocation])
      );

      return this.planForPhase(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        "Balancing admitted nodes before normal dispatch.",
        "none",
        undefined,
        (node) => {
          if (!this.admittedNodeIds.has(node.nodeId)) {
            return buildCommand(node, this.config.supervisionTimeoutMs, commandSequence);
          }

          const allocation = allocationByNode.get(node.nodeId);
          return buildCommand(
            node,
            this.config.supervisionTimeoutMs,
            commandSequence,
            {
              mode:
                allocation && allocation.chargeCurrentA > 0
                  ? "cluster_balancing"
                  : "standby",
              chargeSetpointA: allocation?.chargeCurrentA ?? 0,
              contactorCommandClosed: true,
              allowBalancing: true
            }
          );
        }
      );
    }

    if (this.phase === "ready") {
      return this.planForPhase(
        timestamp,
        nodes,
        commandSequence,
        alerts,
        syntheticFaults,
        "Startup equalization is complete.",
        "none",
        undefined,
        (node) =>
          this.admittedNodeIds.has(node.nodeId)
            ? buildCommand(node, this.config.supervisionTimeoutMs, commandSequence, {
                mode: "standby",
                contactorCommandClosed: true
              })
            : buildCommand(node, this.config.supervisionTimeoutMs, commandSequence)
      );
    }

    return this.failedPlan(
      timestamp,
      nodes,
      commandSequence,
      alerts,
      syntheticFaults,
      "Startup entered a failed state."
    );
  }

  private phaseElapsedMs(nowMs: number): number {
    return nowMs - (this.phaseEnteredAtMs ?? nowMs);
  }

  private transition(nextPhase: StartupPhase, nowMs: number): void {
    this.phase = nextPhase;
    this.phaseEnteredAtMs = nowMs;
  }

  private failedPlan(
    timestamp: string,
    nodes: NodeStatusFrame[],
    commandSequence: number,
    alerts: ClusterAlert[],
    syntheticFaults: ClusterFaultRecord[],
    reason: string
  ): StartupPlan {
    const shouldEmitFailureAlert =
      this.phase !== "failed" || this.lastFailureReason !== reason;
    this.phase = "failed";
    this.lastFailureReason = reason;
    if (shouldEmitFailureAlert) {
      alerts.push(
        buildAlert(
          this.config.siteId,
          this.config.clusterId,
          timestamp,
          "startup_sequence_fault",
          "critical",
          reason,
          "opened"
        )
      );
    }
    if (
      !syntheticFaults.some(
        (fault) =>
          fault.code === "startup_sequence_fault" &&
          fault.message === reason &&
          !fault.nodeId
      )
    ) {
      syntheticFaults.push(
        buildFault(timestamp, "startup_sequence_fault", "critical", reason)
      );
    }

    return this.planForPhase(
      timestamp,
      nodes,
      commandSequence,
      alerts,
      syntheticFaults,
      reason,
      "hold_open"
    );
  }

  private planForPhase(
    timestamp: string,
    nodes: NodeStatusFrame[],
    commandSequence: number,
    alerts: ClusterAlert[],
    syntheticFaults: ClusterFaultRecord[],
    reason: string,
    inverterAction: StartupPlan["inverterAction"],
    inverterPrechargeTargetV?: number,
    commandBuilder?: (node: NodeStatusFrame) => NodeCommandFrame
  ): StartupPlan {
    const commands = nodes.map((node) =>
      commandBuilder
        ? commandBuilder(node)
        : buildCommand(node, this.config.supervisionTimeoutMs, commandSequence)
    );

    for (const blockedNodeId of this.blockedNodeIds) {
      if (syntheticFaults.some((fault) => fault.nodeId === blockedNodeId)) {
        continue;
      }

      syntheticFaults.push(
        buildFault(
          timestamp,
          "voltage_mismatch",
          "warning",
          `Node ${blockedNodeId} remains isolated because it did not match the startup voltage window.`,
          blockedNodeId
        )
      );
    }

    return {
      commands,
      phase: this.phase,
      startupStatus: {
        phase: this.phase,
        primaryNodeId: this.primaryNodeId,
        admittedNodeIds: [...this.admittedNodeIds],
        blockedNodeIds: [...this.blockedNodeIds],
        pendingNodeIds: this.pendingNodeIds.filter(
          (nodeId) =>
            !this.admittedNodeIds.has(nodeId) && !this.blockedNodeIds.has(nodeId)
        ),
        reason
      },
      alerts,
      activeFaults: syntheticFaults,
      ready: this.phase === "ready",
      failed: this.phase === "failed",
      inverterAction,
      inverterPrechargeTargetV
    };
  }
}
