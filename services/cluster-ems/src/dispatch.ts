import type {
  DispatchAllocation,
  DispatchStrategy,
  NodeCommandFrame,
  NodeMode,
  NodeStatusFrame
} from "@clusterstore/contracts";

type DispatchDirection = "charge" | "discharge";

interface DispatchRequest {
  strategy: DispatchStrategy;
  direction: DispatchDirection;
  availableCurrentA: number;
  nodes: NodeStatusFrame[];
  maxCurrentPerNodeA: number;
  supervisionTimeoutMs: number;
}

function clamp(value: number, min: number, max: number): number {
  return Math.max(min, Math.min(max, value));
}

function healthyDispatchNodes(
  nodes: NodeStatusFrame[],
  supervisionTimeoutMs: number
): NodeStatusFrame[] {
  return nodes.filter(
    (node) =>
      node.faultFlags.length === 0 &&
      !node.maintenanceLockout &&
      !node.serviceLockout &&
      node.readyForConnection &&
      node.heartbeatAgeMs < supervisionTimeoutMs
  );
}

function computeWeights(
  strategy: DispatchStrategy,
  direction: DispatchDirection,
  nodes: NodeStatusFrame[]
): number[] {
  if (strategy === "equal_current") {
    return nodes.map(() => 1);
  }

  if (strategy === "soc_weighted") {
    return nodes.map((node) => {
      if (direction === "charge") {
        return Math.max(1, 100 - node.socPct);
      }

      return Math.max(1, node.socPct);
    });
  }

  return nodes.map((node) => {
    const celsius = node.temperatureDeciC / 10;
    return clamp(60 - celsius, 1, 60);
  });
}

export function allocateCurrent(request: DispatchRequest): DispatchAllocation[] {
  const nodes = healthyDispatchNodes(request.nodes, request.supervisionTimeoutMs);
  if (nodes.length === 0 || request.availableCurrentA <= 0) {
    return [];
  }

  const weights = computeWeights(request.strategy, request.direction, nodes);
  const remaining = nodes.map((node, index) => ({
    node,
    weight: weights[index],
    currentA: 0,
    capped: false
  }));

  let distributableCurrentA = request.availableCurrentA;
  while (distributableCurrentA > 0.0001) {
    const uncapped = remaining.filter((entry) => !entry.capped);
    if (uncapped.length === 0) {
      break;
    }

    const totalWeight = uncapped.reduce((sum, entry) => sum + entry.weight, 0);
    let assignedThisRound = 0;

    for (const entry of uncapped) {
      const share = distributableCurrentA * (entry.weight / totalWeight);
      const headroom = request.maxCurrentPerNodeA - entry.currentA;
      const assign = clamp(share, 0, headroom);
      entry.currentA += assign;
      assignedThisRound += assign;
      if (entry.currentA >= request.maxCurrentPerNodeA - 0.0001) {
        entry.capped = true;
      }
    }

    if (assignedThisRound <= 0.0001) {
      break;
    }

    distributableCurrentA -= assignedThisRound;
  }

  return remaining.map((entry) => ({
    nodeId: entry.node.nodeId,
    nodeAddress: entry.node.nodeAddress,
    chargeCurrentA: request.direction === "charge" ? entry.currentA : 0,
    dischargeCurrentA: request.direction === "discharge" ? entry.currentA : 0,
    reason:
      request.strategy === "equal_current"
        ? "mvp_equal_split"
        : request.strategy
  }));
}

function modeForAllocation(allocation: DispatchAllocation): NodeMode {
  if (allocation.chargeCurrentA > 0) {
    return "cluster_slave_charge";
  }

  if (allocation.dischargeCurrentA > 0) {
    return "cluster_slave_discharge";
  }

  return "standby";
}

export function buildNodeCommands(
  nodes: NodeStatusFrame[],
  allocations: DispatchAllocation[],
  supervisionTimeoutMs: number,
  commandSequence: number
): NodeCommandFrame[] {
  const allocationByNode = new Map(
    allocations.map((allocation) => [allocation.nodeId, allocation])
  );

  return nodes.map((node) => {
    const allocation = allocationByNode.get(node.nodeId);
    if (!allocation) {
      const shouldIsolate =
        node.faultFlags.length > 0 ||
        node.maintenanceLockout ||
        node.serviceLockout ||
        node.heartbeatAgeMs >= supervisionTimeoutMs;

      return {
        nodeAddress: node.nodeAddress,
        nodeId: node.nodeId,
        mode: shouldIsolate ? "cluster_isolated" : "standby",
        chargeSetpointA: 0,
        dischargeSetpointA: 0,
        contactorCommandClosed: !shouldIsolate,
        allowBalancing: false,
        supervisionTimeoutMs,
        commandSequence
      };
    }

    return {
      nodeAddress: node.nodeAddress,
      nodeId: node.nodeId,
      mode: modeForAllocation(allocation),
      chargeSetpointA: allocation.chargeCurrentA,
      dischargeSetpointA: allocation.dischargeCurrentA,
      contactorCommandClosed: true,
      allowBalancing: allocation.chargeCurrentA > 0,
      supervisionTimeoutMs,
      commandSequence
    };
  });
}
