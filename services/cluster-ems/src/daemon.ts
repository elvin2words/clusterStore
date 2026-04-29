import { loadClusterEmsDaemonConfig, ClusterEmsDaemon, parseClusterEmsCliOptions } from "./runtime.ts";

async function main(): Promise<void> {
  const options = parseClusterEmsCliOptions(process.argv.slice(2));
  const config = await loadClusterEmsDaemonConfig(options.configPath);
  const daemon = new ClusterEmsDaemon(config);

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
      `[EMS] listening on http://${config.http.host}:${String(config.http.port)}`
    );
  }
}

main().catch((error) => {
  console.error("[EMS] fatal startup error", error);
  process.exitCode = 1;
});
