import { writeFile } from "node:fs/promises";
import { setTimeout as delay } from "node:timers/promises";
import { FakeMqttBroker } from "../tests/support/fake-mqtt-broker.mjs";

function parseArgs(argv) {
  const options = {
    host: "127.0.0.1",
    port: 1883,
    messagesPath: ""
  };

  for (let index = 0; index < argv.length; index += 1) {
    const token = argv[index];
    const value = argv[index + 1];
    if (token === "--host" && value) {
      options.host = value;
      index += 1;
      continue;
    }
    if (token === "--port" && value) {
      options.port = Number.parseInt(value, 10);
      index += 1;
      continue;
    }
    if (token === "--messages" && value) {
      options.messagesPath = value;
      index += 1;
    }
  }

  return options;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const broker = new FakeMqttBroker();
  const endpoint = await broker.start({
    host: options.host,
    port: options.port
  });

  let flushTimer;
  if (options.messagesPath) {
    const flushMessages = async () => {
      await writeFile(
        options.messagesPath,
        `${JSON.stringify(broker.messages, null, 2)}\n`,
        "utf8"
      );
    };

    await flushMessages();
    flushTimer = setInterval(() => {
      void flushMessages();
    }, 200);
  }

  const shutdown = async () => {
    if (flushTimer) {
      clearInterval(flushTimer);
    }
    if (options.messagesPath) {
      await writeFile(
        options.messagesPath,
        `${JSON.stringify(broker.messages, null, 2)}\n`,
        "utf8"
      );
    }
    await broker.stop();
    await delay(25);
    process.exit(0);
  };

  process.on("SIGINT", () => {
    void shutdown();
  });
  process.on("SIGTERM", () => {
    void shutdown();
  });

  console.log(JSON.stringify(endpoint));
}

main().catch((error) => {
  console.error("[fake-mqtt-broker] fatal error", error);
  process.exitCode = 1;
});
