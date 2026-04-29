import assert from "node:assert/strict";
import {
  activateBootSlot,
  buildG474NodeLayout,
  InMemoryPersistentStateStore,
  selectBootSlot,
  SLOT_A,
  SLOT_B
} from "./support/firmware-binding-runtime.mjs";

const testCases = [];

function test(name, fn) {
  testCases.push({ name, fn });
}

test("Dual-slot boot control rolls back to the confirmed slot after trial attempts are exhausted", () => {
  const layout = buildG474NodeLayout();
  const store = new InMemoryPersistentStateStore({
    layout,
    journalCapacity: 16,
    defaultActiveSlotId: SLOT_A,
    defaultVersion: "1.0.0"
  });

  let state = store.load();
  state.bootControl = activateBootSlot(
    state.bootControl,
    layout,
    SLOT_B,
    "1.1.0",
    180 * 1024,
    0x55aa11ff,
    1
  );
  state = store.save(state);

  let decision = selectBootSlot(state.bootControl);
  assert.equal(decision.action, "boot_active");
  assert.equal(decision.slotId, SLOT_B);

  decision = selectBootSlot(state.bootControl);
  assert.equal(decision.action, "boot_fallback");
  assert.equal(decision.slotId, SLOT_A);
  assert.equal(state.bootControl.activeSlotId, SLOT_A);
});

test("Persistent metadata recovers from one corrupted copy and restores the previous journal state", () => {
  const layout = buildG474NodeLayout();
  const store = new InMemoryPersistentStateStore({
    layout,
    journalCapacity: 4,
    defaultActiveSlotId: SLOT_A,
    defaultVersion: "1.0.0"
  });

  const records = [
    {
      sequence: 1,
      timestampMs: 100,
      eventCode: 2,
      severity: 0
    },
    {
      sequence: 2,
      timestampMs: 200,
      eventCode: 10,
      severity: 1
    }
  ];
  const metadata = {
    head: 0,
    count: records.length,
    nextSequence: 3
  };

  const firstSavedState = store.flushJournal(store.load(), records, metadata);
  let secondState = store.save({
    ...firstSavedState,
    bootControl: activateBootSlot(
      firstSavedState.bootControl,
      layout,
      SLOT_B,
      "1.1.0",
      180 * 1024,
      0x55aa11ff,
      2
    )
  });
  store.corruptCopy(secondState.activeCopyIndex);
  secondState = store.load();

  assert.equal(secondState.generation, firstSavedState.generation);
  assert.deepEqual(secondState.journalMetadata, metadata);
  assert.deepEqual(store.restoreJournal(), records);
});

test("G474 layout matches the agreed boot, BCB, journal, and dual-slot addresses", () => {
  const layout = buildG474NodeLayout();

  assert.equal(layout.bootloaderAddress, 0x08000000);
  assert.equal(layout.bcbPrimaryAddress, 0x08008000);
  assert.equal(layout.bcbShadowAddress, 0x08009000);
  assert.equal(layout.journalMetaAAddress, 0x0800a000);
  assert.equal(layout.journalMetaBAddress, 0x0800a800);
  assert.equal(layout.journalRecordAreaAddress, 0x0800b000);
  assert.equal(layout.slotAAddress, 0x08010000);
  assert.equal(layout.slotBAddress, 0x08040000);
  assert.equal(layout.reservedAddress, 0x08070000);
  assert.equal(layout.endAddress, 0x08080000);
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
