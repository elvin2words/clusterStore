import type {
  NodeCommandFrame,
  NodeDiagnosticFrame,
  NodeStatusFrame
} from "@clusterstore/contracts";

export interface CanBusPort {
  readStatuses(): Promise<NodeStatusFrame[]>;
  readDiagnostics(): Promise<NodeDiagnosticFrame[]>;
  writeCommands(commands: NodeCommandFrame[]): Promise<void>;
  isolateNode(nodeId: string): Promise<void>;
}

