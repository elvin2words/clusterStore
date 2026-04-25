import type { ClusterAlert, ClusterTelemetry } from "@clusterstore/contracts";

export interface HmiPort {
  render(snapshot: ClusterTelemetry, alerts: ClusterAlert[]): Promise<void>;
}

