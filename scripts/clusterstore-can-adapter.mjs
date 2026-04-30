import { appendFile } from "node:fs/promises";
import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { dirname, isAbsolute, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ENV_PLACEHOLDER_PATTERN = /\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}/g;
const FULL_ENV_PLACEHOLDER_PATTERN = /^\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}$/;

function parseArgs(argv) {
  const options = {
    configPath: ""
  };
  let command = "";

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if ((argument === "--config" || argument === "-c") && argv[index + 1]) {
      options.configPath = argv[index + 1];
      index += 1;
      continue;
    }

    if (!command) {
      command = argument;
    }
  }

  if (!options.configPath || !command) {
    throw new Error(
      "Expected --config <path> followed by one of: read-statuses, read-diagnostics, write-commands, isolate-node."
    );
  }

  return {
    ...options,
    command
  };
}

function resolveMaybeRelative(configDirectory, filePath) {
  if (!filePath || typeof filePath !== "string") {
    return filePath;
  }

  return isAbsolute(filePath) ? filePath : resolve(configDirectory, filePath);
}

function resolveEnvPlaceholdersInString(template) {
  let missingEnvironmentVariable = "";
  const resolved = template.replace(
    ENV_PLACEHOLDER_PATTERN,
    (_match, name, fallbackGroup, fallbackValue) => {
      const envValue = process.env[name];
      if (envValue !== undefined && envValue !== "") {
        return envValue;
      }

      if (fallbackGroup !== undefined) {
        return fallbackValue ?? "";
      }

      missingEnvironmentVariable = name;
      return "";
    }
  );

  if (missingEnvironmentVariable) {
    throw new Error(
      `Missing required environment variable '${missingEnvironmentVariable}' while loading CAN adapter config.`
    );
  }

  if (FULL_ENV_PLACEHOLDER_PATTERN.test(template)) {
    try {
      return JSON.parse(resolved);
    } catch {
      return resolved;
    }
  }

  return resolved;
}

function resolveEnvPlaceholders(value) {
  if (typeof value === "string") {
    return resolveEnvPlaceholdersInString(value);
  }

  if (Array.isArray(value)) {
    return value.map((entry) => resolveEnvPlaceholders(entry));
  }

  if (typeof value === "object" && value !== null) {
    return Object.fromEntries(
      Object.entries(value).map(([key, entry]) => [key, resolveEnvPlaceholders(entry)])
    );
  }

  return value;
}

async function readJsonFile(filePath, fallback) {
  try {
    const content = await readFile(filePath, "utf8");
    return JSON.parse(content);
  } catch (error) {
    if (error && typeof error === "object" && error.code === "ENOENT") {
      return fallback;
    }
    throw error;
  }
}

async function ensureParentDirectory(filePath) {
  await mkdir(dirname(filePath), { recursive: true });
}

async function writeJsonFileAtomic(filePath, value) {
  await ensureParentDirectory(filePath);
  const tempPath = `${filePath}.tmp`;
  await writeFile(tempPath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
  await rename(tempPath, filePath);
}

async function appendJsonLine(filePath, value) {
  await ensureParentDirectory(filePath);
  await appendFile(filePath, `${JSON.stringify(value)}\n`, "utf8");
}

async function isNodeAlreadyIsolated(isolatesPath, nodeId) {
  try {
    const content = await readFile(isolatesPath, "utf8");
    return content.split("\n").some((line) => {
      try {
        return JSON.parse(line).nodeId === nodeId;
      } catch {
        return false;
      }
    });
  } catch (error) {
    if (error && typeof error === "object" && error.code === "ENOENT") {
      return false;
    }
    throw error;
  }
}

async function readJsonFromStdin() {
  const chunks = [];
  for await (const chunk of process.stdin) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }

  if (chunks.length === 0) {
    return undefined;
  }

  return JSON.parse(Buffer.concat(chunks).toString("utf8"));
}

export async function loadCanAdapterConfig(configPath) {
  const rawConfig = resolveEnvPlaceholders(await readJsonFile(configPath, null));
  if (!rawConfig) {
    throw new Error(`CAN adapter config file not found: ${configPath}`);
  }

  const configDirectory = dirname(configPath);
  return {
    statusesPath: resolveMaybeRelative(configDirectory, rawConfig.statusesPath),
    diagnosticsPath: resolveMaybeRelative(configDirectory, rawConfig.diagnosticsPath),
    commandsPath: resolveMaybeRelative(configDirectory, rawConfig.commandsPath),
    commandHistoryPath: resolveMaybeRelative(configDirectory, rawConfig.commandHistoryPath),
    isolatesPath: resolveMaybeRelative(configDirectory, rawConfig.isolatesPath)
  };
}

export async function executeCanAdapterCommand(configPath, command, input) {
  const config = await loadCanAdapterConfig(resolve(configPath));

  switch (command) {
    case "read-statuses":
      return readJsonFile(config.statusesPath, []);
    case "read-diagnostics":
      return readJsonFile(config.diagnosticsPath, []);
    case "write-commands":
      await writeJsonFileAtomic(config.commandsPath, input ?? []);
      if (config.commandHistoryPath) {
        await appendJsonLine(config.commandHistoryPath, {
          timestamp: new Date().toISOString(),
          commands: input ?? []
        });
      }
      return undefined;
    case "isolate-node":
      if (!input?.nodeId) {
        throw new Error("Expected input with a nodeId property for isolate-node.");
      }
      if (config.isolatesPath) {
        const alreadyIsolated = await isNodeAlreadyIsolated(config.isolatesPath, input.nodeId);
        if (!alreadyIsolated) {
          await appendJsonLine(config.isolatesPath, {
            timestamp: new Date().toISOString(),
            nodeId: input.nodeId
          });
        }
      }
      return undefined;
    default:
      throw new Error(`Unsupported CAN adapter command '${command}'.`);
  }
}

function isMainModule() {
  if (!process.argv[1]) {
    return false;
  }

  return resolve(process.argv[1]) === fileURLToPath(import.meta.url);
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const result = await executeCanAdapterCommand(
    options.configPath,
    options.command,
    await readJsonFromStdin()
  );

  if (result !== undefined) {
    process.stdout.write(`${JSON.stringify(result, null, 2)}\n`);
  }
}

if (isMainModule()) {
  main().catch((error) => {
    console.error(
      `[can-adapter] ${error instanceof Error ? error.message : String(error)}`
    );
    process.exitCode = 1;
  });
}
