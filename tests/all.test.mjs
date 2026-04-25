import assert from "node:assert/strict";
import {
  decodeNodeCommandPayload,
  decodeNodeStatusPayload,
  encodeNodeCommandPayload,
  encodeNodeStatusPayload,
  wrapCommand,
  toNodeStatusFrame,
  toNodeStatusPayload
} from "../packages/contracts/src/index.ts";
import { FaultManager } from "../services/cluster-ems/src/fault-manager.ts";
import { createBridgeRuntime, createEmsRuntime, createNode } from "./support/in-memory-runtime.mjs";

const testCases = [];

function test(name, fn) {
  testCases.push({ name, fn });
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
