import {
  loadUtilityCoreBridgeDaemonConfig,
  parseUtilityCoreBridgeCliOptions,
  UtilityCoreBridgeDaemon
} from "./runtime.ts";

async function main(): Promise<void> {
  const options = parseUtilityCoreBridgeCliOptions(process.argv.slice(2));
  const config = await loadUtilityCoreBridgeDaemonConfig(options.configPath);
  const daemon = new UtilityCoreBridgeDaemon(config);

  const shutdown = async (): Promise<void> => {
    await daemon.stop();
    process.exit(0);
  };

  process.on("SIGINT", () => {
    void shutdown();
  });
  process.on("SIGTERM", () => {
    void shutdown();
  });

  await daemon.start({
    once: options.once
  });

  if (!options.once) {
    console.log(
      `[Bridge] listening on http://${config.http.host}:${String(config.http.port)}`
    );
  }
}

main().catch((error) => {
  console.error("[Bridge] fatal startup error", error);
  process.exitCode = 1;
});
