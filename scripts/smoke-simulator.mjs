import { createBridgeRuntime } from "../tests/support/in-memory-runtime.mjs";

const runtime = createBridgeRuntime();

await runtime.bridge.bindCommandSubscription();
for (let cycle = 0; cycle < 8; cycle += 1) {
  await runtime.emsRuntime.controller.runCycle();
  runtime.clock.advanceMs(1_000);
}

await runtime.bridge.publishCycle();
const snapshot = await runtime.emsRuntime.controller.getSnapshot();

console.log(
  JSON.stringify(
    {
      clusterMode: snapshot.clusterMode,
      startupPhase: snapshot.startupStatus?.phase ?? "complete",
      mqttMessages: runtime.mqtt.messages.length,
      journalEvents: runtime.journal.events.length + runtime.emsRuntime.journal.events.length
    },
    null,
    2
  )
);
