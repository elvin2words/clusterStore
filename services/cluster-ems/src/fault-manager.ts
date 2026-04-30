import type { ClusterAlert, ClusterFaultRecord, NodeStatusFrame } from "@clusterstore/contracts";

const MAX_ACTIVE_INCIDENTS = 256;

interface FaultIncidentState {
  record: ClusterFaultRecord;
}

export interface FaultEvaluation {
  activeFaults: ClusterFaultRecord[];
  alerts: ClusterAlert[];
  isolatedNodeIds: string[];
  clusterDegraded: boolean;
}

function incidentKey(code: string, nodeId?: string): string {
  return nodeId ? `${code}:${nodeId}` : code;
}

function alertId(incident: string, state: ClusterAlert["state"], timestamp: string): string {
  return `${incident}:${state}:${timestamp}`;
}

export class FaultManager {
  private readonly activeIncidents = new Map<string, FaultIncidentState>();

  public evaluate(
    siteId: string,
    clusterId: string,
    timestamp: string,
    nodes: NodeStatusFrame[],
    supervisionTimeoutMs: number
  ): FaultEvaluation {
    const observed = new Map<string, ClusterFaultRecord>();
    const alerts: ClusterAlert[] = [];
    const isolatedNodeIds = new Set<string>();

    for (const node of nodes) {
      if (node.heartbeatAgeMs >= supervisionTimeoutMs) {
        isolatedNodeIds.add(node.nodeId);
        const key = incidentKey("communication_timeout", node.nodeId);
        observed.set(key, {
          incidentKey: key,
          code: "communication_timeout",
          severity: "critical",
          source: "node",
          nodeId: node.nodeId,
          firstObservedAt: timestamp,
          lastObservedAt: timestamp,
          message: "Node heartbeat timeout exceeded supervision window."
        });
      }

      for (const faultFlag of node.faultFlags) {
        isolatedNodeIds.add(node.nodeId);
        const key = incidentKey(faultFlag, node.nodeId);
        observed.set(key, {
          incidentKey: key,
          code: faultFlag,
          severity: "critical",
          source: "node",
          nodeId: node.nodeId,
          firstObservedAt: timestamp,
          lastObservedAt: timestamp,
          message: `Node ${node.nodeId} reported ${faultFlag}.`
        });
      }
    }

    for (const [key, record] of observed) {
      const existing = this.activeIncidents.get(key);
      if (!existing) {
        if (this.activeIncidents.size >= MAX_ACTIVE_INCIDENTS) {
          continue;
        }
        this.activeIncidents.set(key, { record });
        alerts.push({
          id: alertId(key, "opened", timestamp),
          incidentKey: key,
          siteId,
          clusterId,
          timestamp,
          severity: record.severity,
          state: "opened",
          source: record.source,
          code: record.code,
          nodeId: record.nodeId,
          message: record.message
        });
        continue;
      }

      existing.record = {
        ...existing.record,
        lastObservedAt: timestamp,
        message: record.message
      };
    }

    for (const [key, incident] of [...this.activeIncidents.entries()]) {
      if (observed.has(key)) {
        continue;
      }

      this.activeIncidents.delete(key);
      alerts.push({
        id: alertId(key, "cleared", timestamp),
        incidentKey: key,
        siteId,
        clusterId,
        timestamp,
        severity: "info",
        state: "cleared",
        source: incident.record.source,
        code: incident.record.code,
        nodeId: incident.record.nodeId,
        message: `Incident ${key} cleared.`
      });
    }

    return {
      activeFaults: [...this.activeIncidents.values()].map((incident) => incident.record),
      alerts,
      isolatedNodeIds: [...isolatedNodeIds],
      clusterDegraded: this.activeIncidents.size > 0
    };
  }

  public activeIncidentCount(): number {
    return this.activeIncidents.size;
  }
}

