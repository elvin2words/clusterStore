import assert from "node:assert/strict";
import {
  OverlayBmsAdapter,
  normalizeOverlayDiagnostic,
  normalizeOverlayStatus
} from "../services/cluster-ems/src/adapters/bms-adapter.ts";

const testCases = [];

function test(name, fn) {
  testCases.push({ name, fn });
}

function createAsset(overrides = {}) {
  return {
    assetId: "asset-01",
    nodeId: "node-01",
    nodeAddress: 1,
    ratedCapacityKwh: 5.12,
    socPct: 61,
    packVoltageMv: 52400,
    packCurrentMa: -1800,
    temperatureDeciC: 276,
    faultFlags: [],
    contactorClosed: true,
    readyForConnection: true,
    balancingActive: false,
    maintenanceLockout: false,
    serviceLockout: false,
    heartbeatAgeMs: 120,
    firmwareVersion: "pylontech-overlay-1.0",
    cellVoltagesMv: [3298, 3301, 3300, 3299],
    cycleCount: 340,
    cumulativeThroughputKwh: 812.5,
    ...overrides
  };
}

test("Overlay status normalization exposes native-node semantics to EMS", () => {
  const status = normalizeOverlayStatus(
    createAsset({
      balancingActive: true,
      faultFlags: ["communication_timeout"]
    })
  );

  assert.deepEqual(status, {
    nodeAddress: 1,
    nodeId: "node-01",
    ratedCapacityKwh: 5.12,
    socPct: 61,
    packVoltageMv: 52400,
    packCurrentMa: -1800,
    temperatureDeciC: 276,
    faultFlags: ["communication_timeout"],
    contactorClosed: true,
    readyForConnection: true,
    balancingActive: true,
    maintenanceLockout: false,
    serviceLockout: false,
    heartbeatAgeMs: 120
  });
});

test("Overlay diagnostics retain vendor-specific diagnostic richness behind the EMS seam", () => {
  const diagnostic = normalizeOverlayDiagnostic(createAsset());

  assert.equal(diagnostic.nodeId, "node-01");
  assert.equal(diagnostic.cycleCount, 340);
  assert.equal(diagnostic.firmwareVersion, "pylontech-overlay-1.0");
  assert.deepEqual(diagnostic.cellVoltagesMv, [3298, 3301, 3300, 3299]);
});

test("Overlay adapter maps EMS commands into per-asset dispatch requests", async () => {
  const writes = [];
  const isolates = [];
  const adapter = new OverlayBmsAdapter({
    async readAssets() {
      return [createAsset(), createAsset({ assetId: "asset-02", nodeId: "node-02", nodeAddress: 2 })];
    },
    async writeDispatchRequests(requests) {
      writes.push(...requests);
    },
    async isolateAsset(assetId) {
      isolates.push(assetId);
    }
  });

  const statuses = await adapter.readStatuses();
  assert.equal(statuses.length, 2);

  await adapter.writeCommands([
    {
      nodeAddress: 2,
      nodeId: "node-02",
      mode: "cluster_slave_charge",
      chargeSetpointA: 8,
      dischargeSetpointA: 0,
      contactorCommandClosed: true,
      allowBalancing: false,
      supervisionTimeoutMs: 1500,
      commandSequence: 42
    }
  ]);

  assert.deepEqual(writes, [
    {
      assetId: "asset-02",
      nodeId: "node-02",
      nodeAddress: 2,
      mode: "cluster_slave_charge",
      chargeSetpointA: 8,
      dischargeSetpointA: 0,
      contactorCommandClosed: true,
      allowBalancing: false,
      supervisionTimeoutMs: 1500,
      commandSequence: 42
    }
  ]);

  await adapter.isolateNode("node-01");
  assert.deepEqual(isolates, ["asset-01"]);
});

test("Overlay adapter rejects conflicting node identifiers instead of matching either one", async () => {
  const adapter = new OverlayBmsAdapter({
    async readAssets() {
      return [
        createAsset(),
        createAsset({ assetId: "asset-02", nodeId: "node-02", nodeAddress: 2 })
      ];
    },
    async writeDispatchRequests() {
      throw new Error("writeDispatchRequests should not be called");
    },
    async isolateAsset() {
      throw new Error("isolateAsset should not be called");
    }
  });

  await adapter.readStatuses();

  await assert.rejects(
    adapter.writeCommands([
      {
        nodeAddress: 2,
        nodeId: "node-01",
        mode: "cluster_slave_charge",
        chargeSetpointA: 8,
        dischargeSetpointA: 0,
        contactorCommandClosed: true,
        allowBalancing: false,
        supervisionTimeoutMs: 1500,
        commandSequence: 42
      }
    ]),
    /Overlay asset not found/
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
