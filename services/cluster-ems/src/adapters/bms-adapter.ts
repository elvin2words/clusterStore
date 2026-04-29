import type {
  FaultCode,
  NodeCommandFrame,
  NodeDiagnosticFrame,
  NodeStatusFrame
} from "@clusterstore/contracts";
import type { CanBusPort } from "./can-bus.ts";

export interface OverlayAssetTelemetry {
  assetId: string;
  nodeId?: string;
  nodeAddress: number;
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
  firmwareVersion?: string;
  cellVoltagesMv?: number[];
  cycleCount?: number;
  cumulativeThroughputKwh?: number;
}

export interface OverlayDispatchRequest {
  assetId: string;
  nodeId: string;
  nodeAddress: number;
  mode: NodeCommandFrame["mode"];
  chargeSetpointA: number;
  dischargeSetpointA: number;
  contactorCommandClosed: boolean;
  allowBalancing: boolean;
  supervisionTimeoutMs: number;
  commandSequence: number;
}

export interface OverlayBmsAssetPort {
  readAssets(): Promise<OverlayAssetTelemetry[]>;
  writeDispatchRequests(requests: OverlayDispatchRequest[]): Promise<void>;
  isolateAsset(assetId: string): Promise<void>;
}

function normalizeNodeId(snapshot: OverlayAssetTelemetry): string {
  return snapshot.nodeId ?? snapshot.assetId;
}

export function normalizeOverlayStatus(
  snapshot: OverlayAssetTelemetry
): NodeStatusFrame {
  return {
    nodeAddress: snapshot.nodeAddress,
    nodeId: normalizeNodeId(snapshot),
    ratedCapacityKwh: snapshot.ratedCapacityKwh,
    socPct: snapshot.socPct,
    packVoltageMv: snapshot.packVoltageMv,
    packCurrentMa: snapshot.packCurrentMa,
    temperatureDeciC: snapshot.temperatureDeciC,
    faultFlags: [...snapshot.faultFlags],
    contactorClosed: snapshot.contactorClosed,
    readyForConnection: snapshot.readyForConnection,
    balancingActive: snapshot.balancingActive,
    maintenanceLockout: snapshot.maintenanceLockout,
    serviceLockout: snapshot.serviceLockout,
    heartbeatAgeMs: snapshot.heartbeatAgeMs
  };
}

export function normalizeOverlayDiagnostic(
  snapshot: OverlayAssetTelemetry
): NodeDiagnosticFrame {
  return {
    nodeAddress: snapshot.nodeAddress,
    nodeId: normalizeNodeId(snapshot),
    cellVoltagesMv: snapshot.cellVoltagesMv ?? [],
    cycleCount: snapshot.cycleCount ?? 0,
    cumulativeThroughputKwh: snapshot.cumulativeThroughputKwh ?? 0,
    balancingActive: snapshot.balancingActive,
    firmwareVersion: snapshot.firmwareVersion ?? "overlay"
  };
}

export class OverlayBmsAdapter implements CanBusPort {
  private readonly assetPort: OverlayBmsAssetPort;
  private snapshotCache = new Map<string, OverlayAssetTelemetry>();

  public constructor(assetPort: OverlayBmsAssetPort) {
    this.assetPort = assetPort;
  }

  public async readStatuses(): Promise<NodeStatusFrame[]> {
    const snapshots = await this.assetPort.readAssets();
    this.snapshotCache = new Map(
      snapshots.map((snapshot) => [normalizeNodeId(snapshot), snapshot])
    );
    return snapshots.map((snapshot) => normalizeOverlayStatus(snapshot));
  }

  public async readDiagnostics(): Promise<NodeDiagnosticFrame[]> {
    const snapshots = await this.readOrReuseSnapshots();
    return snapshots.map((snapshot) => normalizeOverlayDiagnostic(snapshot));
  }

  public async writeCommands(commands: NodeCommandFrame[]): Promise<void> {
    const snapshots = await this.readOrReuseSnapshots();
    const requests = commands.map((command) => {
      const snapshot = this.resolveSnapshot(snapshots, command.nodeId, command.nodeAddress);
      return {
        assetId: snapshot.assetId,
        nodeId: normalizeNodeId(snapshot),
        nodeAddress: snapshot.nodeAddress,
        mode: command.mode,
        chargeSetpointA: command.chargeSetpointA,
        dischargeSetpointA: command.dischargeSetpointA,
        contactorCommandClosed: command.contactorCommandClosed,
        allowBalancing: command.allowBalancing,
        supervisionTimeoutMs: command.supervisionTimeoutMs,
        commandSequence: command.commandSequence
      };
    });

    await this.assetPort.writeDispatchRequests(requests);
  }

  public async isolateNode(nodeId: string): Promise<void> {
    const snapshots = await this.readOrReuseSnapshots();
    const snapshot = this.resolveSnapshot(snapshots, nodeId);
    await this.assetPort.isolateAsset(snapshot.assetId);
  }

  private async readOrReuseSnapshots(): Promise<OverlayAssetTelemetry[]> {
    if (this.snapshotCache.size > 0) {
      return [...this.snapshotCache.values()];
    }

    const snapshots = await this.assetPort.readAssets();
    this.snapshotCache = new Map(
      snapshots.map((snapshot) => [normalizeNodeId(snapshot), snapshot])
    );
    return snapshots;
  }

  private resolveSnapshot(
    snapshots: OverlayAssetTelemetry[],
    nodeId?: string,
    nodeAddress?: number
  ): OverlayAssetTelemetry {
    const snapshot = snapshots.find(
      (candidate) =>
        (nodeId === undefined || normalizeNodeId(candidate) === nodeId) &&
        (nodeAddress === undefined || candidate.nodeAddress === nodeAddress)
    );
    if (!snapshot) {
      const target =
        nodeId !== undefined && nodeAddress !== undefined
          ? `${nodeId} @ ${String(nodeAddress)}`
          : nodeId ?? String(nodeAddress);
      throw new Error(
        `Overlay asset not found for node ${target}.`
      );
    }

    return snapshot;
  }
}
