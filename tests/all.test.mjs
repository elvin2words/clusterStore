import assert from "node:assert/strict";
import { createServer } from "node:http";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import path from "node:path";
import { tmpdir } from "node:os";
import {
  commandAckTopic,
  commandsTopic,
  decodeNodeCommandPayload,
  decodeNodeStatusPayload,
  encodeNodeCommandPayload,
  encodeNodeStatusPayload,
  telemetryTopic,
  wrapCommand,
  toNodeStatusFrame,
  toNodeStatusPayload
} from "../packages/contracts/src/index.ts";
import { FaultManager } from "../services/cluster-ems/src/fault-manager.ts";
import {
  ClusterEmsDaemon,
  ModbusTcpGridInverterPort,
  loadClusterEmsDaemonConfig
} from "../services/cluster-ems/src/runtime.ts";
import {
  UtilityCoreBridgeDaemon,
  loadUtilityCoreBridgeDaemonConfig
} from "../services/utilitycore-bridge/src/runtime.ts";
import {
  executeCanAdapterCommand
} from "../scripts/clusterstore-can-adapter.mjs";
import {
  executeWatchdogAdapterCommand
} from "../scripts/clusterstore-watchdog-adapter.mjs";
import {
  createBridgeRuntime,
  createDefaultConfig,
  createEmsRuntime,
  createNode
} from "./support/in-memory-runtime.mjs";
import { FakeModbusServer } from "./support/fake-modbus-server.mjs";
import { FakeMqttBroker } from "./support/fake-mqtt-broker.mjs";

const testCases = [];

function test(name, fn) {
  testCases.push({ name, fn });
}

function sleep(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

async function createTempWorkspace(prefix) {
  return mkdtemp(path.join(tmpdir(), `${prefix}-`));
}

async function startFakeEmsApiServer() {
  const snapshot = {
    siteId: "site-alpha",
    clusterId: "cluster-01",
    timestamp: "2026-04-28T09:00:00.000Z",
    clusterMode: "normal_dispatch",
    dispatchStrategy: "equal_current",
    aggregateSocPct: 58,
    aggregateCapacityKwh: 10.24,
    availableEnergyKwh: 5.939,
    chargePowerW: 1200,
    dischargePowerW: 0,
    cumulativeEnergyInKwh: 1.2,
    cumulativeEnergyOutKwh: 0.4,
    dataQuality: "good",
    socComputation: "fresh_nodes",
    powerComputation: "site_meter",
    freshNodeCount: 2,
    staleNodeCount: 0,
    nodeEstimatedPowerW: 1180,
    meteredSitePowerW: 1200,
    powerMismatchW: 20,
    incidentCount: 0,
    nodeSocBreakdown: [
      {
        nodeId: "node-01",
        nodeAddress: 1,
        capacityKwh: 5.12,
        socPct: 57,
        temperatureDeciC: 275,
        contactorClosed: true,
        readyForConnection: true,
        serviceLockout: false,
        faultFlags: []
      },
      {
        nodeId: "node-02",
        nodeAddress: 2,
        capacityKwh: 5.12,
        socPct: 59,
        temperatureDeciC: 278,
        contactorClosed: true,
        readyForConnection: true,
        serviceLockout: false,
        faultFlags: []
      }
    ],
    acOutputVoltageV: 230,
    acOutputFrequencyHz: 50,
    acOutputLoadW: 1200,
    solarGenerationW: 250,
    activeFaults: []
  };
  let pendingAlerts = [
    {
      id: "alert-001",
      incidentKey: "alert-001",
      siteId: "site-alpha",
      clusterId: "cluster-01",
      timestamp: "2026-04-28T09:00:00.000Z",
      severity: "warning",
      state: "opened",
      source: "bridge",
      code: "lte_fault",
      message: "LTE RSSI dropped below preferred threshold."
    }
  ];
  const receivedCommands = [];
  const server = createServer(async (request, response) => {
    const url = new URL(request.url ?? "/", "http://127.0.0.1");
    if ((request.method ?? "GET") === "GET" && url.pathname === "/snapshot") {
      response.writeHead(200, { "content-type": "application/json" });
      response.end(JSON.stringify(snapshot));
      return;
    }
    if ((request.method ?? "GET") === "GET" && url.pathname === "/alerts") {
      const alerts = pendingAlerts;
      if (url.searchParams.get("drain") === "true") {
        pendingAlerts = [];
      }
      response.writeHead(200, { "content-type": "application/json" });
      response.end(JSON.stringify(alerts));
      return;
    }
    if ((request.method ?? "GET") === "POST" && url.pathname === "/commands") {
      const chunks = [];
      for await (const chunk of request) {
        chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
      }
      const command = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      receivedCommands.push(command);
      response.writeHead(200, { "content-type": "application/json" });
      response.end(
        JSON.stringify({
          commandId: command.id,
          idempotencyKey: command.idempotencyKey,
          status: "completed",
          timestamp: snapshot.timestamp,
          appliedClusterMode: snapshot.clusterMode
        })
      );
      return;
    }

    response.writeHead(404, { "content-type": "application/json" });
    response.end(JSON.stringify({ error: "not found" }));
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      server.off("error", reject);
      resolve();
    });
  });

  const address = server.address();
  const baseUrl = `http://${address.address}:${String(address.port)}`;
  return {
    baseUrl,
    receivedCommands,
    async stop() {
      await new Promise((resolve) => {
        server.close(() => resolve());
      });
    }
  };
}

test("CAN payload helpers round-trip the aligned wire contract", () => {
  const statusFrame = createNode({
    nodeAddress: 3,
    nodeId: "node-03",
    socPct: 62,
    packVoltageMv: 52110,
    packCurrentMa: -2400,
    temperatureDeciC: 280,
    contactorClosed: true,
    readyForConnection: true
  });

  const statusPayload = toNodeStatusPayload(statusFrame);
  const statusBytes = encodeNodeStatusPayload(statusPayload);
  assert.equal(statusBytes.byteLength, 8);

  const decodedStatus = decodeNodeStatusPayload(statusBytes);
  const normalizedStatus = toNodeStatusFrame(3, decodedStatus, {
    ratedCapacityKwh: 5,
    heartbeatAgeMs: 40,
    nodeId: "node-03"
  });

  assert.equal(normalizedStatus.nodeId, "node-03");
  assert.equal(normalizedStatus.packVoltageMv, 52110);
  assert.equal(normalizedStatus.contactorClosed, true);
  assert.equal(normalizedStatus.readyForConnection, true);
  assert.equal(normalizedStatus.serviceLockout, false);

  const commandBytes = encodeNodeCommandPayload({
    nodeAddress: 3,
    nodeId: "node-03",
    mode: "cluster_balancing",
    chargeSetpointA: 4.2,
    dischargeSetpointA: 0,
    contactorCommandClosed: true,
    allowBalancing: true,
    supervisionTimeoutMs: 1_500,
    commandSequence: 12
  });

  assert.equal(commandBytes.byteLength, 8);
  const decodedCommand = decodeNodeCommandPayload(3, commandBytes);
  assert.equal(decodedCommand.mode, "cluster_balancing");
  assert.equal(decodedCommand.contactorCommandClosed, true);
  assert.equal(decodedCommand.allowBalancing, true);
  assert.equal(decodedCommand.commandSequence, 12);
});

test("EMS startup sequencer reaches dispatch-ready state without skipping safe phases", async () => {
  const runtime = createEmsRuntime({
    nodes: [
      createNode({ nodeAddress: 1, socPct: 48, packVoltageMv: 51200 }),
      createNode({ nodeAddress: 2, socPct: 50, packVoltageMv: 51300 })
    ]
  });

  let telemetry = await runtime.controller.runCycle();
  assert.equal(telemetry.clusterMode, "startup_equalization");
  assert.ok(
    ["precharge_primary", "close_primary"].includes(
      telemetry.startupStatus?.phase ?? ""
    )
  );

  runtime.clock.advanceMs(1_000);
  telemetry = await runtime.controller.runCycle();
  runtime.clock.advanceMs(1_000);
  telemetry = await runtime.controller.runCycle();
  runtime.clock.advanceMs(1_000);
  telemetry = await runtime.controller.runCycle();

  assert.ok(
    ["startup_equalization", "normal_dispatch", "degraded"].includes(
      telemetry.clusterMode
    )
  );
  assert.ok(runtime.watchdog.kickCount >= 4);
  assert.ok(runtime.canBus.commandHistory.length >= 4);
  assert.ok(runtime.inverter.prechargeTargets.length >= 1);
});

test("EMS waits for enough healthy nodes instead of latching startup failure immediately", async () => {
  const runtime = createEmsRuntime({
    nodes: [createNode({ nodeAddress: 1, socPct: 48, packVoltageMv: 51200 })]
  });

  let telemetry = await runtime.controller.runCycle();
  assert.equal(telemetry.clusterMode, "startup_equalization");
  assert.equal(telemetry.startupStatus?.phase, "discover_nodes");
  assert.match(
    telemetry.startupStatus?.reason ?? "",
    /Waiting for enough healthy nodes/
  );

  runtime.canBus.nodes.push(
    createNode({ nodeAddress: 2, socPct: 50, packVoltageMv: 51300 })
  );
  runtime.clock.advanceMs(1_000);
  telemetry = await runtime.controller.runCycle();

  assert.equal(telemetry.clusterMode, "startup_equalization");
  assert.ok(
    ["precharge_primary", "close_primary"].includes(
      telemetry.startupStatus?.phase ?? ""
    )
  );
});

test("Fault incidents open once and clear once instead of alerting every cycle", () => {
  const manager = new FaultManager();
  const siteId = "site-alpha";
  const clusterId = "cluster-01";
  const nodes = [
    createNode({
      nodeAddress: 1,
      nodeId: "node-01",
      faultFlags: ["over_temperature"]
    }),
    createNode({ nodeAddress: 2, nodeId: "node-02" })
  ];

  const first = manager.evaluate(
    siteId,
    clusterId,
    "2026-04-24T10:00:00.000Z",
    nodes,
    1_500
  );
  assert.equal(first.alerts.filter((alert) => alert.state === "opened").length, 1);

  const second = manager.evaluate(
    siteId,
    clusterId,
    "2026-04-24T10:00:01.000Z",
    nodes,
    1_500
  );
  assert.equal(second.alerts.length, 0);

  nodes[0].faultFlags = [];
  const third = manager.evaluate(
    siteId,
    clusterId,
    "2026-04-24T10:00:02.000Z",
    nodes,
    1_500
  );
  assert.equal(third.alerts.filter((alert) => alert.state === "cleared").length, 1);
});

test("Bridge buffers telemetry offline and replays it with acknowledgements once LTE recovers", async () => {
  const runtime = createBridgeRuntime();
  await runtime.bridge.bindCommandSubscription();

  runtime.lte.online = false;
  await runtime.bridge.publishCycle();
  assert.equal(runtime.buffer.pending.length >= 1, true);

  runtime.lte.online = true;
  await runtime.bridge.publishCycle();
  assert.equal(runtime.buffer.pending.length, 0);
  assert.equal(runtime.mqtt.messages.length >= 1, true);
});

test("Bridge keeps live telemetry buffered if replay fails before publish completes", async () => {
  const runtime = createBridgeRuntime();
  await runtime.bridge.bindCommandSubscription();

  await runtime.buffer.enqueue({
    id: "backlog-1",
    topic: "cluster/site-alpha/cluster-01/telemetry",
    payload: "{\"backlog\":true}",
    createdAt: runtime.clock.now().toISOString()
  });

  runtime.mqtt.failNextPublish = true;
  await runtime.bridge.publishCycle();

  assert.equal(runtime.buffer.pending.some((message) => message.id === "backlog-1"), true);
  assert.equal(
    runtime.buffer.pending.some((message) => message.id.startsWith("telemetry:")),
    true
  );
});

test("Bridge command flow rejects unsafe EMS commands after accepted transport validation", async () => {
  const runtime = createBridgeRuntime();
  await runtime.bridge.bindCommandSubscription();

  const command = {
    id: "cmd-001",
    idempotencyKey: "cmd-001",
    sequence: 1,
    type: "force_charge",
    createdAt: runtime.clock.now().toISOString(),
    expiresAt: new Date(runtime.clock.now().getTime() + 60_000).toISOString(),
    requestedBy: "fleet@test",
    target: {
      siteId: "site-alpha",
      clusterId: "cluster-01"
    },
    authorization: {
      tokenId: "token-01",
      role: "fleet_controller",
      scopes: ["cluster:force_charge"],
      issuedAt: runtime.clock.now().toISOString(),
      expiresAt: new Date(runtime.clock.now().getTime() + 60_000).toISOString()
    },
    payload: {
      currentA: 10
    }
  };

  await runtime.mqtt.emit(
    "cluster/site-alpha/cluster-01/cmd",
    JSON.stringify(wrapCommand(runtime.clock.now().toISOString(), command))
  );

  const acknowledgements = runtime.commandLedger.acks.map((ack) => ack.status);
  assert.deepEqual(acknowledgements, ["accepted", "rejected"]);
});

test("Bridge rejects per-node commands until targeted dispatch is implemented", async () => {
  const runtime = createBridgeRuntime();
  await runtime.bridge.bindCommandSubscription();

  const command = {
    id: "cmd-002",
    idempotencyKey: "cmd-002",
    sequence: 2,
    type: "set_maintenance_mode",
    createdAt: runtime.clock.now().toISOString(),
    expiresAt: new Date(runtime.clock.now().getTime() + 60_000).toISOString(),
    requestedBy: "service@test",
    target: {
      siteId: "site-alpha",
      clusterId: "cluster-01",
      nodeIds: ["node-01"]
    },
    authorization: {
      tokenId: "token-02",
      role: "service",
      scopes: ["cluster:set_maintenance_mode"],
      issuedAt: runtime.clock.now().toISOString(),
      expiresAt: new Date(runtime.clock.now().getTime() + 60_000).toISOString()
    },
    payload: {
      enabled: true
    }
  };

  await runtime.mqtt.emit(
    "cluster/site-alpha/cluster-01/cmd",
    JSON.stringify(wrapCommand(runtime.clock.now().toISOString(), command))
  );

  assert.deepEqual(runtime.commandLedger.acks.map((ack) => ack.status), ["rejected"]);
  assert.match(
    runtime.commandLedger.acks[0].reason ?? "",
    /Per-node targeting is not yet supported/
  );
});

test("Modbus TCP inverter adapter reads live state and writes setpoints", async () => {
  const server = new FakeModbusServer({
    1: 2300,
    2: 500,
    3: 2295,
    4: 500,
    5: 1200,
    6: 512,
    7: 1,
    8: 250,
    9: 180,
    10: 0,
    11: 0,
    12: 2,
    13: 1190
  });
  const endpoint = await server.start();
  const inverter = new ModbusTcpGridInverterPort({
    kind: "modbus-tcp",
    host: endpoint.host,
    port: endpoint.port,
    unitId: 1,
    stateMap: {
      acInputVoltageV: { address: 1, type: "u16", scale: 10 },
      acInputFrequencyHz: { address: 2, type: "u16", scale: 10 },
      acOutputVoltageV: { address: 3, type: "u16", scale: 10 },
      acOutputFrequencyHz: { address: 4, type: "u16", scale: 10 },
      acOutputLoadW: { address: 5, type: "u16" },
      dcBusVoltageV: { address: 6, type: "u16", scale: 10 },
      gridAvailable: { address: 7, type: "bool" },
      solarGenerationW: { address: 8, type: "u16" },
      availableChargeCurrentA: { address: 9, type: "u16", scale: 10 },
      requestedDischargeCurrentA: { address: 10, type: "u16", scale: 10 },
      exportAllowed: { address: 11, type: "bool" },
      tariffBand: {
        address: 12,
        type: "tariff-band",
        values: {
          "0": "cheap",
          "1": "normal",
          "2": "expensive"
        }
      },
      meteredSitePowerW: { address: 13, type: "u16" }
    },
    setpointMap: {
      operatingMode: {
        address: 100,
        type: "enum",
        values: {
          idle: 0,
          charge: 1,
          discharge: 2,
          grid_support: 3
        }
      },
      aggregateChargeCurrentA: { address: 101, type: "u16", scale: 10 },
      aggregateDischargeCurrentA: { address: 102, type: "u16", scale: 10 },
      exportLimitW: { address: 103, type: "u16" },
      prechargeTargetVoltageV: { address: 104, type: "u16", scale: 10 },
      holdOpenBus: { address: 105, type: "bool", trueValue: 1, falseValue: 0 }
    }
  });

  try {
    const state = await inverter.readState();
    assert.equal(state.acInputVoltageV, 230);
    assert.equal(state.availableChargeCurrentA, 18);
    assert.equal(state.tariffBand, "expensive");
    assert.equal(state.meteredSitePowerW, 1190);

    await inverter.writeSetpoint({
      operatingMode: "charge",
      aggregateChargeCurrentA: 12.3,
      aggregateDischargeCurrentA: 0,
      exportLimitW: 900
    });
    await inverter.prechargeDcBus(52.4);
    await inverter.holdOpenBus();

    assert.deepEqual(server.getWrites(), [
      { address: 100, value: 1 },
      { address: 101, value: 123 },
      { address: 102, value: 0 },
      { address: 103, value: 900 },
      { address: 104, value: 524 },
      { address: 105, value: 1 }
    ]);
  } finally {
    await server.stop();
  }
});

test("EMS daemon exposes runnable HTTP control and file-backed outputs", async () => {
  const workspace = await createTempWorkspace("clusterstore-ems");
  const statusesPath = path.join(workspace, "statuses.json");
  const diagnosticsPath = path.join(workspace, "diagnostics.json");
  const inverterStatePath = path.join(workspace, "inverter-state.json");
  const commandsPath = path.join(workspace, "commands.json");
  const snapshotPath = path.join(workspace, "hmi-snapshot.json");
  const watchdogPath = path.join(workspace, "watchdog.json");
  const journalPath = path.join(workspace, "journal.jsonl");

  await writeFile(
    statusesPath,
    JSON.stringify([
      createNode({ nodeAddress: 1, socPct: 48, packVoltageMv: 51200 }),
      createNode({ nodeAddress: 2, socPct: 50, packVoltageMv: 51300 })
    ])
  );
  await writeFile(diagnosticsPath, "[]");
  await writeFile(
    inverterStatePath,
    JSON.stringify({
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
      tariffBand: "normal"
    })
  );

  const daemon = new ClusterEmsDaemon({
    config: createDefaultConfig(),
    cycle: {
      intervalMs: 30_000,
      runOnStart: false
    },
    http: {
      host: "127.0.0.1",
      port: 0
    },
    canBus: {
      kind: "state-file",
      statusesPath,
      diagnosticsPath,
      commandsPath
    },
    inverter: {
      kind: "state-file",
      statePath: inverterStatePath,
      setpointPath: path.join(workspace, "setpoint.json")
    },
    hmi: {
      kind: "file",
      snapshotPath
    },
    watchdog: {
      kind: "file",
      heartbeatPath: watchdogPath
    },
    journal: {
      kind: "jsonl-file",
      path: journalPath
    }
  });

  try {
    await daemon.start();
    const address = daemon.snapshotState().httpAddress;
    assert.ok(address);

    const cycleResponse = await fetch(`${address}/run-cycle`, { method: "POST" });
    assert.equal(cycleResponse.status, 200);
    const cycleSnapshot = await cycleResponse.json();
    assert.equal(cycleSnapshot.siteId, "site-alpha");

    const snapshotResponse = await fetch(`${address}/snapshot`);
    assert.equal(snapshotResponse.status, 200);
    const snapshot = await snapshotResponse.json();
    assert.equal(snapshot.clusterId, "cluster-01");

    const diagnosticsResponse = await fetch(`${address}/diagnostics`);
    assert.equal(diagnosticsResponse.status, 200);
    assert.deepEqual(await diagnosticsResponse.json(), []);

    const commands = JSON.parse(await readFile(commandsPath, "utf8"));
    assert.equal(commands.length, 2);
    assert.equal(JSON.parse(await readFile(snapshotPath, "utf8")).siteId, "site-alpha");
  } finally {
    await daemon.stop();
    await rm(workspace, { recursive: true, force: true });
  }
});

test("Bridge daemon publishes telemetry over MQTT and routes remote commands to the EMS API", async () => {
  const workspace = await createTempWorkspace("clusterstore-bridge");
  const broker = new FakeMqttBroker();
  const mqttEndpoint = await broker.start();
  const emsApi = await startFakeEmsApiServer();

  await writeFile(
    path.join(workspace, "lte.json"),
    JSON.stringify({ online: true, rssi: -66 })
  );

  const daemon = new UtilityCoreBridgeDaemon({
    bridge: {
      siteId: "site-alpha",
      clusterId: "cluster-01",
      maxCommandTtlMs: 15 * 60_000,
      replayBatchSize: 50
    },
    publish: {
      intervalMs: 30_000,
      runOnStart: false
    },
    http: {
      host: "127.0.0.1",
      port: 0
    },
    mqtt: {
      kind: "mqtt-tcp",
      host: mqttEndpoint.host,
      port: mqttEndpoint.port,
      clientId: "clusterstore-bridge-test"
    },
    lte: {
      kind: "state-file",
      path: path.join(workspace, "lte.json")
    },
    emsApi: {
      baseUrl: emsApi.baseUrl
    },
    buffer: {
      kind: "file",
      path: path.join(workspace, "buffer.json")
    },
    scada: {
      kind: "file",
      telemetryPath: path.join(workspace, "scada-telemetry.json"),
      alertsPath: path.join(workspace, "scada-alerts.jsonl")
    },
    authorizer: {
      kind: "allow-all"
    },
    commandLedger: {
      kind: "file",
      path: path.join(workspace, "command-ledger.json")
    },
    journal: {
      kind: "jsonl-file",
      path: path.join(workspace, "bridge-journal.jsonl")
    }
  });

  try {
    await daemon.start();
    await daemon.publishCycle();
    await sleep(75);

    const publishedTelemetry = broker.messages.find(
      (message) => message.topic === telemetryTopic("site-alpha", "cluster-01")
    );
    assert.ok(publishedTelemetry);

    const now = new Date();
    const expiresAt = new Date(now.getTime() + 5 * 60_000).toISOString();
    const command = {
      id: "cmd-live-001",
      idempotencyKey: "cmd-live-001",
      sequence: 1,
      type: "set_maintenance_mode",
      createdAt: now.toISOString(),
      expiresAt,
      requestedBy: "service@test",
      target: {
        siteId: "site-alpha",
        clusterId: "cluster-01"
      },
      authorization: {
        tokenId: "token-live-001",
        role: "service",
        scopes: ["cluster:set_maintenance_mode"],
        issuedAt: now.toISOString(),
        expiresAt
      },
      payload: {
        enabled: true
      }
    };

    await broker.publish(
      commandsTopic("site-alpha", "cluster-01"),
      JSON.stringify(wrapCommand(now.toISOString(), command))
    );
    await sleep(150);

    assert.equal(emsApi.receivedCommands.length, 1);
    assert.equal(emsApi.receivedCommands[0].id, "cmd-live-001");

    const ackStatuses = broker.messages
      .filter((message) => message.topic === commandAckTopic("site-alpha", "cluster-01"))
      .map((message) => JSON.parse(message.payload).payload.status);
    assert.deepEqual(ackStatuses, ["accepted", "completed"]);

    const scadaSnapshot = JSON.parse(
      await readFile(path.join(workspace, "scada-telemetry.json"), "utf8")
    );
    assert.equal(scadaSnapshot.clusterId, "cluster-01");
  } finally {
    await daemon.stop();
    await broker.stop();
    await emsApi.stop();
    await rm(workspace, { recursive: true, force: true });
  }
});

test("CAN adapter CLI reads state and records command-side effects", async () => {
  const workspace = await createTempWorkspace("clusterstore-can-adapter");
  const configPath = path.join(workspace, "can-adapter.json");
  const statusesPath = path.join(workspace, "statuses.json");
  const diagnosticsPath = path.join(workspace, "diagnostics.json");
  const commandsPath = path.join(workspace, "commands.json");
  const commandHistoryPath = path.join(workspace, "command-history.jsonl");
  const isolatesPath = path.join(workspace, "isolates.jsonl");

  await writeFile(
    statusesPath,
    JSON.stringify([
      {
        nodeAddress: 1,
        nodeId: "node-01",
        ratedCapacityKwh: 5,
        socPct: 50,
        packVoltageMv: 51200,
        packCurrentMa: 0,
        temperatureDeciC: 250,
        faultFlags: [],
        contactorClosed: true,
        readyForConnection: true,
        balancingActive: false,
        maintenanceLockout: false,
        serviceLockout: false,
        heartbeatAgeMs: 0
      }
    ]),
    "utf8"
  );
  await writeFile(
    diagnosticsPath,
    JSON.stringify([
      {
        nodeId: "node-01",
        incidentKey: "diag-01",
        faultFlags: [],
        timestamp: "2026-04-29T01:00:00.000Z"
      }
    ]),
    "utf8"
  );
  await writeFile(
    configPath,
    JSON.stringify(
      {
        statusesPath,
        diagnosticsPath,
        commandsPath,
        commandHistoryPath,
        isolatesPath
      },
      null,
      2
    ),
    "utf8"
  );

  try {
    const statusesResult = await executeCanAdapterCommand(
      configPath,
      "read-statuses"
    );
    assert.equal(statusesResult[0].nodeId, "node-01");

    const diagnosticsResult = await executeCanAdapterCommand(
      configPath,
      "read-diagnostics"
    );
    assert.equal(diagnosticsResult[0].incidentKey, "diag-01");

    await executeCanAdapterCommand(configPath, "write-commands", [
      {
        nodeId: "node-01",
        command: "set_current",
        requestedCurrentA: 12
      }
    ]);

    const writtenCommands = JSON.parse(await readFile(commandsPath, "utf8"));
    assert.equal(writtenCommands[0].requestedCurrentA, 12);

    await executeCanAdapterCommand(configPath, "isolate-node", {
      nodeId: "node-02"
    });

    const isolateHistory = (await readFile(isolatesPath, "utf8")).trim().split("\n");
    assert.equal(JSON.parse(isolateHistory[0]).nodeId, "node-02");
  } finally {
    await rm(workspace, { recursive: true, force: true });
  }
});

test("Watchdog adapter CLI writes heartbeat and fail-safe records", async () => {
  const workspace = await createTempWorkspace("clusterstore-watchdog-adapter");
  const configPath = path.join(workspace, "watchdog-adapter.json");
  const heartbeatPath = path.join(workspace, "watchdog-heartbeat.json");
  const failSafePath = path.join(workspace, "watchdog-failsafe.jsonl");

  await writeFile(
    configPath,
    JSON.stringify(
      {
        heartbeatPath,
        failSafePath
      },
      null,
      2
    ),
    "utf8"
  );

  try {
    await executeWatchdogAdapterCommand(configPath, "kick");
    const heartbeat = JSON.parse(await readFile(heartbeatPath, "utf8"));
    assert.equal(heartbeat.ok, true);

    await executeWatchdogAdapterCommand(configPath, "trigger-failsafe", {
      reason: "integration-test"
    });
    const failSafeEntries = (await readFile(failSafePath, "utf8")).trim().split("\n");
    assert.equal(JSON.parse(failSafeEntries[0]).reason, "integration-test");
  } finally {
    await rm(workspace, { recursive: true, force: true });
  }
});

test("Daemon config loaders resolve environment placeholders and numeric overrides", async () => {
  const workspace = await createTempWorkspace("clusterstore-config-loaders");
  const emsConfigPath = path.join(workspace, "ems-config.json");
  const bridgeConfigPath = path.join(workspace, "bridge-config.json");

  const originalEnv = {
    TEST_CLUSTERSTORE_SITE_ID: process.env.TEST_CLUSTERSTORE_SITE_ID,
    TEST_CLUSTERSTORE_MQTT_PASSWORD: process.env.TEST_CLUSTERSTORE_MQTT_PASSWORD,
    TEST_CLUSTERSTORE_BRIDGE_PORT: process.env.TEST_CLUSTERSTORE_BRIDGE_PORT
  };

  process.env.TEST_CLUSTERSTORE_SITE_ID = "site-zeta";
  process.env.TEST_CLUSTERSTORE_MQTT_PASSWORD = "secret-pass";
  process.env.TEST_CLUSTERSTORE_BRIDGE_PORT = "8099";

  await writeFile(
    emsConfigPath,
    JSON.stringify(
      {
        config: {
          siteId: "${TEST_CLUSTERSTORE_SITE_ID}",
          clusterId: "cluster-zeta",
          aggregateCapacityKwh: 10,
          maxChargeCurrentPerNodeA: 20,
          maxDischargeCurrentPerNodeA: 20,
          equalizationWindowPct: 5,
          controlLoopIntervalMs: 1000,
          telemetryIntervalMs: 60000,
          supervisionTimeoutMs: 1500,
          defaultDispatchStrategy: "equal_current",
          startup: {
            voltageMatchWindowMv: 500,
            prechargeTimeoutMs: 3000,
            contactorSettleTimeoutMs: 1000,
            balancingTimeoutMs: 5000,
            balancingMaxCurrentA: 5,
            startupTimeoutMs: 10000,
            minNodesForDispatch: 2
          },
          remoteCommands: {
            maxCommandTtlMs: 900000,
            maxChargeOverrideCurrentA: 20,
            maxDischargeOverrideCurrentA: 20,
            allowedRolesByType: {
              set_dispatch_mode: ["service"]
            }
          }
        },
        cycle: {
          intervalMs: 1000
        },
        http: {
          host: "127.0.0.1",
          port: 8081
        },
        canBus: {
          kind: "command",
          readStatuses: {
            command: "node",
            cwd: ".",
            args: ["scripts/clusterstore-can-adapter.mjs", "--config", "./can.json", "read-statuses"]
          },
          writeCommands: {
            command: "node",
            cwd: ".",
            args: ["scripts/clusterstore-can-adapter.mjs", "--config", "./can.json", "write-commands"]
          },
          isolateNode: {
            command: "node",
            cwd: ".",
            args: ["scripts/clusterstore-can-adapter.mjs", "--config", "./can.json", "isolate-node"]
          }
        },
        inverter: {
          kind: "state-file",
          statePath: "./inverter-state.json",
          setpointPath: "./inverter-setpoint.json"
        },
        hmi: {
          kind: "file",
          snapshotPath: "./hmi-snapshot.json"
        },
        watchdog: {
          kind: "command",
          kick: {
            command: "node",
            cwd: ".",
            args: ["scripts/clusterstore-watchdog-adapter.mjs", "--config", "./watchdog.json", "kick"]
          },
          triggerFailSafe: {
            command: "node",
            cwd: ".",
            args: ["scripts/clusterstore-watchdog-adapter.mjs", "--config", "./watchdog.json", "trigger-failsafe"]
          }
        },
        journal: {
          kind: "jsonl-file",
          path: "./journal.jsonl"
        }
      },
      null,
      2
    ),
    "utf8"
  );

  await writeFile(
    bridgeConfigPath,
    JSON.stringify(
      {
        bridge: {
          siteId: "${TEST_CLUSTERSTORE_SITE_ID}",
          clusterId: "cluster-zeta",
          maxCommandTtlMs: 900000,
          replayBatchSize: 50
        },
        publish: {
          intervalMs: 1000
        },
        http: {
          host: "127.0.0.1",
          port: "${TEST_CLUSTERSTORE_BRIDGE_PORT}"
        },
        mqtt: {
          kind: "mqtt-tcp",
          host: "broker.example.net",
          port: 8883,
          clientId: "bridge-zeta",
          username: "bridge-zeta",
          password: "${TEST_CLUSTERSTORE_MQTT_PASSWORD}"
        },
        lte: {
          kind: "state-file",
          path: "./lte.json"
        },
        emsApi: {
          baseUrl: "http://127.0.0.1:8081"
        },
        buffer: {
          kind: "file",
          path: "./buffer.json"
        },
        scada: {
          kind: "file",
          telemetryPath: "./scada.json"
        },
        authorizer: {
          kind: "policy",
          allowedRoles: ["service"],
          allowedRequesters: ["requester@test"]
        },
        commandLedger: {
          kind: "file",
          path: "./ledger.json"
        },
        journal: {
          kind: "jsonl-file",
          path: "./journal.jsonl"
        }
      },
      null,
      2
    ),
    "utf8"
  );

  try {
    const emsConfig = await loadClusterEmsDaemonConfig(emsConfigPath);
    const bridgeConfig = await loadUtilityCoreBridgeDaemonConfig(bridgeConfigPath);

    assert.equal(emsConfig.config.siteId, "site-zeta");
    assert.equal(bridgeConfig.bridge.siteId, "site-zeta");
    assert.equal(bridgeConfig.mqtt.password, "secret-pass");
    assert.equal(bridgeConfig.http.port, 8099);
  } finally {
    if (originalEnv.TEST_CLUSTERSTORE_SITE_ID === undefined) {
      delete process.env.TEST_CLUSTERSTORE_SITE_ID;
    } else {
      process.env.TEST_CLUSTERSTORE_SITE_ID = originalEnv.TEST_CLUSTERSTORE_SITE_ID;
    }

    if (originalEnv.TEST_CLUSTERSTORE_MQTT_PASSWORD === undefined) {
      delete process.env.TEST_CLUSTERSTORE_MQTT_PASSWORD;
    } else {
      process.env.TEST_CLUSTERSTORE_MQTT_PASSWORD = originalEnv.TEST_CLUSTERSTORE_MQTT_PASSWORD;
    }

    if (originalEnv.TEST_CLUSTERSTORE_BRIDGE_PORT === undefined) {
      delete process.env.TEST_CLUSTERSTORE_BRIDGE_PORT;
    } else {
      process.env.TEST_CLUSTERSTORE_BRIDGE_PORT = originalEnv.TEST_CLUSTERSTORE_BRIDGE_PORT;
    }

    await rm(workspace, { recursive: true, force: true });
  }
});

let failures = 0;
for (const testCase of testCases) {
  try {
    await testCase.fn();
    console.log(`PASS ${testCase.name}`);
  } catch (error) {
    failures += 1;
    console.error(`FAIL ${testCase.name}`);
    console.error(error);
  }
}

if (failures > 0) {
  process.exitCode = 1;
}
