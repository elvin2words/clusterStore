import { ClusterEmsController } from "../../services/cluster-ems/src/index.ts";
import { UtilityCoreBridgeService } from "../../services/utilitycore-bridge/src/index.ts";

export function createNode(overrides = {}) {
  return {
    nodeAddress: overrides.nodeAddress ?? 1,
    nodeId: overrides.nodeId ?? `node-${String(overrides.nodeAddress ?? 1).padStart(2, "0")}`,
    ratedCapacityKwh: overrides.ratedCapacityKwh ?? 5,
    socPct: overrides.socPct ?? 50,
    packVoltageMv: overrides.packVoltageMv ?? 51200,
    packCurrentMa: overrides.packCurrentMa ?? 0,
    temperatureDeciC: overrides.temperatureDeciC ?? 250,
    faultFlags: overrides.faultFlags ?? [],
    contactorClosed: overrides.contactorClosed ?? false,
    readyForConnection: overrides.readyForConnection ?? true,
    balancingActive: overrides.balancingActive ?? false,
    maintenanceLockout: overrides.maintenanceLockout ?? false,
    serviceLockout: overrides.serviceLockout ?? false,
    heartbeatAgeMs: overrides.heartbeatAgeMs ?? 0
  };
}

export function createDefaultConfig() {
  return {
    siteId: "site-alpha",
    clusterId: "cluster-01",
    aggregateCapacityKwh: 10,
    maxChargeCurrentPerNodeA: 20,
    maxDischargeCurrentPerNodeA: 20,
    equalizationWindowPct: 5,
    controlLoopIntervalMs: 1_000,
    telemetryIntervalMs: 60_000,
    supervisionTimeoutMs: 1_500,
    defaultDispatchStrategy: "equal_current",
    startup: {
      voltageMatchWindowMv: 500,
      prechargeTimeoutMs: 3_000,
      contactorSettleTimeoutMs: 1_000,
      balancingTimeoutMs: 5_000,
      balancingMaxCurrentA: 5,
      startupTimeoutMs: 10_000,
      minNodesForDispatch: 2
    },
    remoteCommands: {
      maxCommandTtlMs: 15 * 60_000,
      maxChargeOverrideCurrentA: 20,
      maxDischargeOverrideCurrentA: 20,
      allowedRolesByType: {
        force_charge: ["fleet_controller", "service"],
        force_discharge: ["fleet_controller", "service"],
        set_dispatch_mode: ["fleet_controller", "service", "operator"],
        set_maintenance_mode: ["service", "technician"],
        clear_fault_latch: ["service", "technician"]
      }
    }
  };
}

export class FixedClock {
  constructor(start = "2026-04-24T10:00:00.000Z") {
    this.current = new Date(start);
  }

  now() {
    return new Date(this.current);
  }

  advanceMs(ms) {
    this.current = new Date(this.current.getTime() + ms);
  }
}

export class InMemoryCanBus {
  constructor(nodes) {
    this.nodes = nodes.map((node) => ({ ...node }));
    this.commandHistory = [];
  }

  async readStatuses() {
    return this.nodes.map((node) => ({ ...node }));
  }

  async readDiagnostics() {
    return [];
  }

  async writeCommands(commands) {
    this.commandHistory.push(commands.map((command) => ({ ...command })));
    for (const command of commands) {
      const node = this.nodes.find((entry) => entry.nodeId === command.nodeId);
      if (!node) {
        continue;
      }

      if (
        command.mode === "cluster_isolated" ||
        !command.contactorCommandClosed ||
        node.maintenanceLockout ||
        node.faultFlags.length > 0
      ) {
        node.contactorClosed = false;
        node.balancingActive = false;
        node.packCurrentMa = 0;
        continue;
      }

      node.contactorClosed = true;
      node.balancingActive = command.allowBalancing;
      if (command.chargeSetpointA > 0) {
        node.packCurrentMa = Math.round(command.chargeSetpointA * 1_000);
        node.socPct = Math.min(100, node.socPct + command.chargeSetpointA * 0.4);
      } else if (command.dischargeSetpointA > 0) {
        node.packCurrentMa = Math.round(command.dischargeSetpointA * -1_000);
        node.socPct = Math.max(0, node.socPct - command.dischargeSetpointA * 0.2);
      } else {
        node.packCurrentMa = 0;
      }
    }
  }

  async isolateNode(nodeId) {
    const node = this.nodes.find((entry) => entry.nodeId === nodeId);
    if (!node) {
      return;
    }

    node.contactorClosed = false;
    node.packCurrentMa = 0;
    node.balancingActive = false;
  }
}

export class InMemoryInverter {
  constructor(overrides = {}) {
    this.state = {
      acInputVoltageV: 230,
      acInputFrequencyHz: 50,
      acOutputVoltageV: 230,
      acOutputFrequencyHz: 50,
      acOutputLoadW: 0,
      dcBusVoltageV: 51.2,
      gridAvailable: true,
      solarGenerationW: 0,
      availableChargeCurrentA: 10,
      requestedDischargeCurrentA: 0,
      exportAllowed: false,
      tariffBand: "normal",
      ...overrides
    };
    this.prechargeTargets = [];
    this.setpoints = [];
    this.holdOpenCount = 0;
  }

  async readState() {
    return { ...this.state };
  }

  async writeSetpoint(setpoint) {
    this.setpoints.push({ ...setpoint });
  }

  async prechargeDcBus(targetVoltageV) {
    this.prechargeTargets.push(targetVoltageV);
    this.state.dcBusVoltageV = targetVoltageV;
  }

  async holdOpenBus() {
    this.holdOpenCount += 1;
  }
}

export class InMemoryHmi {
  constructor() {
    this.frames = [];
  }

  async render(snapshot, alerts) {
    this.frames.push({ snapshot, alerts });
  }
}

export class InMemoryWatchdog {
  constructor() {
    this.kickCount = 0;
    this.failSafeReasons = [];
  }

  async kick() {
    this.kickCount += 1;
  }

  async triggerFailSafe(reason) {
    this.failSafeReasons.push(reason);
  }
}

export class InMemoryJournal {
  constructor() {
    this.events = [];
  }

  async record(event) {
    this.events.push(event);
  }
}

export class InMemoryMqttBroker {
  constructor() {
    this.messages = [];
    this.subscriptions = new Map();
    this.failNextPublish = false;
  }

  async publish(topic, payload) {
    if (this.failNextPublish) {
      this.failNextPublish = false;
      throw new Error("Simulated MQTT publish failure.");
    }

    this.messages.push({ topic, payload });
  }

  async subscribe(topic, handler) {
    this.subscriptions.set(topic, handler);
  }

  async emit(topic, payload) {
    const handler = this.subscriptions.get(topic);
    if (!handler) {
      throw new Error(`No handler registered for topic ${topic}.`);
    }

    await handler(payload);
  }
}

export class InMemoryLteModem {
  constructor() {
    this.online = true;
  }

  async isOnline() {
    return this.online;
  }

  async signalQualityRssi() {
    return -70;
  }
}

export class InMemoryScada {
  constructor() {
    this.telemetry = [];
    this.alerts = [];
  }

  async publishTelemetry(snapshot) {
    this.telemetry.push(snapshot);
  }

  async publishAlerts(alerts) {
    this.alerts.push(alerts);
  }
}

export class InMemoryTelemetryBuffer {
  constructor() {
    this.pending = [];
  }

  async enqueue(message) {
    if (!this.pending.some((entry) => entry.id === message.id)) {
      this.pending.push({ ...message });
    }
  }

  async peekPending(limit) {
    return this.pending.slice(0, limit).map((entry) => ({ ...entry }));
  }

  async acknowledge(messageIds) {
    this.pending = this.pending.filter(
      (entry) => !messageIds.includes(entry.id)
    );
  }
}

export class AllowAllAuthorizer {
  async authorize() {
    return { authorized: true };
  }
}

export class InMemoryCommandLedger {
  constructor() {
    this.received = [];
    this.acks = [];
    this.seen = new Set();
  }

  async hasSeen(idempotencyKey) {
    return this.seen.has(idempotencyKey);
  }

  async recordReceived(command) {
    this.received.push(command);
  }

  async recordAcknowledgement(acknowledgement) {
    this.acks.push(acknowledgement);
    this.seen.add(acknowledgement.idempotencyKey);
  }
}

export function createEmsRuntime({
  nodes = [createNode({ nodeAddress: 1, socPct: 48 }), createNode({ nodeAddress: 2, socPct: 50 })],
  inverterOverrides = {},
  clock = new FixedClock()
} = {}) {
  const canBus = new InMemoryCanBus(nodes);
  const inverter = new InMemoryInverter(inverterOverrides);
  const hmi = new InMemoryHmi();
  const watchdog = new InMemoryWatchdog();
  const journal = new InMemoryJournal();
  const controller = new ClusterEmsController({
    config: createDefaultConfig(),
    canBus,
    inverter,
    hmi,
    watchdog,
    clock,
    journal
  });

  return {
    controller,
    canBus,
    inverter,
    hmi,
    watchdog,
    journal,
    clock
  };
}

export function createBridgeRuntime({
  emsRuntime = createEmsRuntime(),
  clock = emsRuntime.clock
} = {}) {
  const mqtt = new InMemoryMqttBroker();
  const lte = new InMemoryLteModem();
  const buffer = new InMemoryTelemetryBuffer();
  const scada = new InMemoryScada();
  const authorizer = new AllowAllAuthorizer();
  const commandLedger = new InMemoryCommandLedger();
  const journal = new InMemoryJournal();
  const bridge = new UtilityCoreBridgeService(
    {
      siteId: "site-alpha",
      clusterId: "cluster-01",
      maxCommandTtlMs: 15 * 60_000,
      replayBatchSize: 50
    },
    clock,
    mqtt,
    lte,
    buffer,
    scada,
    authorizer,
    commandLedger,
    journal,
    emsRuntime.controller
  );

  return {
    bridge,
    mqtt,
    lte,
    buffer,
    scada,
    authorizer,
    commandLedger,
    journal,
    clock,
    emsRuntime
  };
}
