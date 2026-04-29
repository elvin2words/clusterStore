import { access, readFile } from "node:fs/promises";
import { constants } from "node:fs";
import { dirname, isAbsolute, resolve } from "node:path";
import { Socket } from "node:net";
import { connect as connectTls } from "node:tls";

function parseArgs(argv) {
  const options = {
    emsConfigPath: "",
    bridgeConfigPath: "",
    probe: false
  };

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if ((argument === "--ems" || argument === "-e") && argv[index + 1]) {
      options.emsConfigPath = argv[index + 1];
      index += 1;
      continue;
    }

    if ((argument === "--bridge" || argument === "-b") && argv[index + 1]) {
      options.bridgeConfigPath = argv[index + 1];
      index += 1;
      continue;
    }

    if (argument === "--probe") {
      options.probe = true;
    }
  }

  if (!options.emsConfigPath || !options.bridgeConfigPath) {
    throw new Error(
      "Expected --ems <path> and --bridge <path>. Optional flag: --probe."
    );
  }

  return options;
}

function resolveMaybeRelative(configDirectory, filePath) {
  if (!filePath || typeof filePath !== "string") {
    return filePath;
  }

  return isAbsolute(filePath) ? filePath : resolve(configDirectory, filePath);
}

async function readJsonFile(filePath) {
  const content = await readFile(filePath, "utf8");
  return JSON.parse(content);
}

const ENV_PLACEHOLDER_PATTERN = /\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}/g;
const FULL_ENV_PLACEHOLDER_PATTERN = /^\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}$/;

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
      `Missing required environment variable '${missingEnvironmentVariable}' while validating configs.`
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

async function fileExists(filePath) {
  try {
    await access(filePath, constants.F_OK);
    return true;
  } catch {
    return false;
  }
}

function addResult(results, level, scope, message) {
  results.push({ level, scope, message });
}

function addPass(results, scope, message) {
  addResult(results, "pass", scope, message);
}

function addWarn(results, scope, message) {
  addResult(results, "warn", scope, message);
}

function addFail(results, scope, message) {
  addResult(results, "fail", scope, message);
}

function isNonEmptyString(value) {
  return typeof value === "string" && value.trim().length > 0;
}

function isPositiveInteger(value) {
  return Number.isInteger(value) && value > 0;
}

function isLikelyFilePath(value) {
  return (
    typeof value === "string" &&
    (value.includes("/") ||
      value.includes("\\") ||
      value.startsWith(".") ||
      /\.(mjs|js|cjs|ps1|py|exe|cmd|bat)$/i.test(value))
  );
}

function resolveCommandWorkingDirectory(configDirectory, config) {
  return resolveMaybeRelative(configDirectory, config.cwd) ?? configDirectory;
}

function resolveCommandTargetPath(commandWorkingDirectory, config) {
  if (isLikelyFilePath(config.command)) {
    return resolveMaybeRelative(commandWorkingDirectory, config.command);
  }

  const firstArgument = Array.isArray(config.args) ? config.args[0] : undefined;
  if (
    typeof firstArgument === "string" &&
    ["node", "node.exe", "python", "python.exe", "powershell", "powershell.exe", "pwsh", "pwsh.exe"].includes(
      String(config.command).toLowerCase()
    ) &&
    isLikelyFilePath(firstArgument)
  ) {
    return resolveMaybeRelative(commandWorkingDirectory, firstArgument);
  }

  return undefined;
}

async function validateCommandInvocation(results, scope, config, description, configDirectory) {
  if (!config || typeof config !== "object") {
    addFail(results, scope, `${description} is missing its command definition.`);
    return;
  }

  if (!isNonEmptyString(config.command)) {
    addFail(results, scope, `${description} must declare a non-empty command.`);
    return;
  }

  if (config.timeoutMs !== undefined && (!Number.isInteger(config.timeoutMs) || config.timeoutMs <= 0)) {
    addFail(results, scope, `${description} timeoutMs must be a positive integer when provided.`);
    return;
  }

  const commandWorkingDirectory = resolveCommandWorkingDirectory(configDirectory, config);
  if (!(await fileExists(commandWorkingDirectory))) {
    addFail(
      results,
      scope,
      `${description} working directory does not exist: '${commandWorkingDirectory}'.`
    );
    return;
  }

  const commandTargetPath = resolveCommandTargetPath(commandWorkingDirectory, config);
  if (commandTargetPath) {
    if (await fileExists(commandTargetPath)) {
      addPass(
        results,
        scope,
        `${description} uses command '${config.command}' with local target '${commandTargetPath}'.`
      );
    } else {
      addFail(
        results,
        scope,
        `${description} references a local command target that does not exist: '${commandTargetPath}'.`
      );
    }
    return;
  }

  addPass(results, scope, `${description} uses command '${config.command}'.`);
}

async function validateEmsConfig(results, config, configPath) {
  const configDirectory = dirname(configPath);
  const emsConfig = config?.config;
  if (!emsConfig || typeof emsConfig !== "object") {
    addFail(results, "EMS", "EMS daemon config is missing the top-level 'config' object.");
    return;
  }

  if (isNonEmptyString(emsConfig.siteId) && isNonEmptyString(emsConfig.clusterId)) {
    addPass(
      results,
      "EMS",
      `EMS identifies site '${emsConfig.siteId}' and cluster '${emsConfig.clusterId}'.`
    );
  } else {
    addFail(results, "EMS", "EMS siteId and clusterId must both be set.");
  }

  if (isPositiveInteger(config?.cycle?.intervalMs)) {
    addPass(results, "EMS", `EMS control cycle is set to ${String(config.cycle.intervalMs)} ms.`);
  } else {
    addFail(results, "EMS", "EMS cycle.intervalMs must be a positive integer.");
  }

  switch (config?.canBus?.kind) {
    case "command":
      await validateCommandInvocation(results, "EMS", config.canBus.readStatuses, "CAN status read adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.canBus.readDiagnostics, "CAN diagnostics read adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.canBus.writeCommands, "CAN command write adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.canBus.isolateNode, "CAN isolate adapter", configDirectory);
      break;
    case "state-file":
      addWarn(results, "EMS", "EMS CAN bus is file-backed; replace it with a live adapter before commissioning.");
      break;
    case "overlay-file":
      addWarn(results, "EMS", "EMS CAN bus uses the overlay file adapter; it is suitable for simulation, not live CAN.");
      break;
    default:
      addFail(results, "EMS", "EMS canBus.kind is missing or unsupported.");
      break;
  }

  switch (config?.inverter?.kind) {
    case "modbus-tcp": {
      const requiredStateFields = [
        "acInputVoltageV",
        "acInputFrequencyHz",
        "acOutputVoltageV",
        "acOutputFrequencyHz",
        "acOutputLoadW",
        "dcBusVoltageV",
        "gridAvailable",
        "solarGenerationW",
        "availableChargeCurrentA",
        "requestedDischargeCurrentA",
        "exportAllowed",
        "tariffBand"
      ];

      if (
        isNonEmptyString(config.inverter.host) &&
        isPositiveInteger(config.inverter.port) &&
        isPositiveInteger(config.inverter.unitId)
      ) {
        addPass(
          results,
          "EMS",
          `EMS inverter targets Modbus TCP ${config.inverter.host}:${String(config.inverter.port)} unit ${String(config.inverter.unitId)}.`
        );
      } else {
        addFail(results, "EMS", "EMS Modbus TCP inverter host, port, and unitId must be set.");
      }

      const missingStateFields = requiredStateFields.filter(
        (field) => !config.inverter.stateMap || !(field in config.inverter.stateMap)
      );
      if (missingStateFields.length === 0) {
        addPass(results, "EMS", "EMS Modbus state map covers all required telemetry fields.");
      } else {
        addFail(
          results,
          "EMS",
          `EMS Modbus state map is missing: ${missingStateFields.join(", ")}.`
        );
      }

      if (config.inverter.setpointMap) {
        addPass(results, "EMS", "EMS Modbus setpoint map is configured for dispatch writes.");
      } else {
        addWarn(results, "EMS", "EMS Modbus inverter is read-only because setpointMap is not configured.");
      }
      break;
    }
    case "command":
      await validateCommandInvocation(results, "EMS", config.inverter.readState, "Inverter state adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.inverter.writeSetpoint, "Inverter setpoint adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.inverter.prechargeDcBus, "Inverter precharge adapter", configDirectory);
      await validateCommandInvocation(results, "EMS", config.inverter.holdOpenBus, "Inverter hold-open adapter", configDirectory);
      break;
    case "state-file":
      addWarn(results, "EMS", "EMS inverter is file-backed; replace it with Modbus or a command adapter before go-live.");
      break;
    default:
      addFail(results, "EMS", "EMS inverter.kind is missing or unsupported.");
      break;
  }

  if (config?.watchdog?.kind === "file") {
    addWarn(
      results,
      "EMS",
      "EMS watchdog is file-backed; confirm another local supervisor consumes that heartbeat in production."
    );
  } else if (config?.watchdog?.kind === "command") {
    await validateCommandInvocation(results, "EMS", config.watchdog.kick, "Watchdog kick adapter", configDirectory);
    await validateCommandInvocation(results, "EMS", config.watchdog.triggerFailSafe, "Watchdog fail-safe adapter", configDirectory);
  } else {
    addFail(results, "EMS", "EMS watchdog.kind is missing or unsupported.");
  }

  if (config?.journal?.kind === "jsonl-file" && isNonEmptyString(config.journal.path)) {
    addPass(
      results,
      "EMS",
      `EMS journal path resolves to '${resolveMaybeRelative(configDirectory, config.journal.path)}'.`
    );
  } else if (config?.journal?.kind === "command") {
    await validateCommandInvocation(results, "EMS", config.journal.record, "EMS journal adapter", configDirectory);
  } else {
    addFail(results, "EMS", "EMS journal.kind is missing or unsupported.");
  }
}

async function validateBridgeConfig(results, config, configPath) {
  const configDirectory = dirname(configPath);
  const bridgeConfig = config?.bridge;
  if (!bridgeConfig || typeof bridgeConfig !== "object") {
    addFail(results, "Bridge", "Bridge daemon config is missing the top-level 'bridge' object.");
    return;
  }

  if (isNonEmptyString(bridgeConfig.siteId) && isNonEmptyString(bridgeConfig.clusterId)) {
    addPass(
      results,
      "Bridge",
      `Bridge identifies site '${bridgeConfig.siteId}' and cluster '${bridgeConfig.clusterId}'.`
    );
  } else {
    addFail(results, "Bridge", "Bridge siteId and clusterId must both be set.");
  }

  if (
    config?.mqtt?.kind === "mqtt-tcp" &&
    isNonEmptyString(config.mqtt.host) &&
    isPositiveInteger(config.mqtt.port) &&
    isNonEmptyString(config.mqtt.clientId)
  ) {
    addPass(
      results,
      "Bridge",
      `Bridge MQTT client '${config.mqtt.clientId}' targets ${config.mqtt.host}:${String(config.mqtt.port)}.`
    );
  } else {
    addFail(results, "Bridge", "Bridge MQTT host, port, clientId, and kind must be configured.");
  }

  if (config?.mqtt?.tls?.enabled) {
    addPass(results, "Bridge", "Bridge MQTT TLS is enabled.");
    if (isNonEmptyString(config.mqtt.tls.caCertPath)) {
      const caCertPath = resolveMaybeRelative(configDirectory, config.mqtt.tls.caCertPath);
      if (await fileExists(caCertPath)) {
        addPass(results, "Bridge", `Bridge CA certificate exists at '${caCertPath}'.`);
      } else {
        addWarn(
          results,
          "Bridge",
          `Bridge CA certificate is referenced but not present yet: '${caCertPath}'.`
        );
      }
    } else {
      addWarn(results, "Bridge", "Bridge MQTT TLS does not yet point at a CA certificate file.");
    }
  } else {
    addWarn(results, "Bridge", "Bridge MQTT TLS is disabled.");
  }

  if (!isNonEmptyString(config?.mqtt?.username)) {
    addWarn(results, "Bridge", "Bridge MQTT username is not configured.");
  }
  if (!isNonEmptyString(config?.mqtt?.password) || config.mqtt.password === "replace-me") {
    addWarn(results, "Bridge", "Bridge MQTT password is still a placeholder or missing.");
  }

  switch (config?.authorizer?.kind) {
    case "policy":
      if (
        Array.isArray(config.authorizer.allowedRoles) &&
        config.authorizer.allowedRoles.length > 0 &&
        Array.isArray(config.authorizer.allowedRequesters) &&
        config.authorizer.allowedRequesters.length > 0
      ) {
        addPass(results, "Bridge", "Bridge command authorization policy is configured.");
      } else {
        addWarn(
          results,
          "Bridge",
          "Bridge policy authorizer is present but its allow-lists are incomplete."
        );
      }
      break;
    case "allow-all":
      addWarn(results, "Bridge", "Bridge authorizer allows every requester; tighten this before production.");
      break;
    default:
      addFail(results, "Bridge", "Bridge authorizer.kind is missing or unsupported.");
      break;
  }

  switch (config?.lte?.kind) {
    case "http-json":
      if (isNonEmptyString(config.lte.url)) {
        addPass(results, "Bridge", `Bridge LTE modem is polled from '${config.lte.url}'.`);
      } else {
        addFail(results, "Bridge", "Bridge http-json LTE adapter must define a URL.");
      }
      break;
    case "command":
      await validateCommandInvocation(results, "Bridge", config.lte.query, "LTE state adapter", configDirectory);
      break;
    case "state-file":
      addWarn(results, "Bridge", "Bridge LTE state is file-backed; replace it with a real modem path before deployment.");
      break;
    default:
      addFail(results, "Bridge", "Bridge lte.kind is missing or unsupported.");
      break;
  }

  if (isNonEmptyString(config?.emsApi?.baseUrl)) {
    addPass(results, "Bridge", `Bridge EMS API points at '${config.emsApi.baseUrl}'.`);
  } else {
    addFail(results, "Bridge", "Bridge emsApi.baseUrl must be configured.");
  }

  for (const [label, pathValue] of [
    ["telemetry buffer", config?.buffer?.path],
    ["command ledger", config?.commandLedger?.path],
    ["bridge journal", config?.journal?.path]
  ]) {
    if (isNonEmptyString(pathValue)) {
      addPass(
        results,
        "Bridge",
        `Bridge ${label} path resolves to '${resolveMaybeRelative(configDirectory, pathValue)}'.`
      );
    } else {
      addFail(results, "Bridge", `Bridge ${label} path must be configured.`);
    }
  }
}

function validateCrossConfig(results, emsConfig, bridgeConfig) {
  const emsIdentity = emsConfig?.config ?? {};
  const bridgeIdentity = bridgeConfig?.bridge ?? {};

  if (
    emsIdentity.siteId === bridgeIdentity.siteId &&
    emsIdentity.clusterId === bridgeIdentity.clusterId
  ) {
    addPass(results, "Cross", "EMS and bridge share the same site and cluster identity.");
  } else {
    addFail(results, "Cross", "EMS and bridge site/cluster identity do not match.");
  }

  const emsCommandTtl = emsIdentity?.remoteCommands?.maxCommandTtlMs;
  const bridgeCommandTtl = bridgeIdentity?.maxCommandTtlMs;
  if (emsCommandTtl === bridgeCommandTtl) {
    addPass(results, "Cross", `Remote command TTL is aligned at ${String(bridgeCommandTtl)} ms.`);
  } else {
    addWarn(
      results,
      "Cross",
      `Remote command TTL differs between EMS (${String(emsCommandTtl)}) and bridge (${String(bridgeCommandTtl)}).`
    );
  }
}

function probeTcpSocket(host, port, timeoutMs) {
  return new Promise((resolvePromise, rejectPromise) => {
    const socket = new Socket();
    const finalize = (callback) => {
      socket.removeAllListeners();
      socket.destroy();
      callback();
    };

    socket.setTimeout(timeoutMs, () => {
      finalize(() => rejectPromise(new Error(`Timed out connecting to ${host}:${String(port)}.`)));
    });
    socket.once("error", (error) => {
      finalize(() => rejectPromise(error));
    });
    socket.connect(port, host, () => {
      finalize(() => resolvePromise());
    });
  });
}

async function probeTlsSocket(host, port, tlsConfig, timeoutMs) {
  return new Promise(async (resolvePromise, rejectPromise) => {
    const options = {
      host,
      port,
      servername: tlsConfig?.servername ?? host,
      rejectUnauthorized: tlsConfig?.rejectUnauthorized ?? true
    };

    if (isNonEmptyString(tlsConfig?.caCertPath)) {
      const caPath = resolve(tlsConfig.caCertPath);
      if (await fileExists(caPath)) {
        options.ca = await readFile(caPath);
      }
    }

    const socket = connectTls(options, () => {
      socket.destroy();
      resolvePromise();
    });

    socket.setTimeout(timeoutMs, () => {
      socket.destroy();
      rejectPromise(new Error(`Timed out connecting to ${host}:${String(port)} over TLS.`));
    });
    socket.once("error", (error) => {
      socket.destroy();
      rejectPromise(error);
    });
  });
}

async function probeHttpJson(url, timeoutMs) {
  const controller = new AbortController();
  const timeoutHandle = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(url, { signal: controller.signal });
    if (!response.ok) {
      throw new Error(`HTTP ${String(response.status)} from ${url}.`);
    }
    await response.text();
  } finally {
    clearTimeout(timeoutHandle);
  }
}

async function runProbes(results, emsConfig, bridgeConfig, bridgeConfigPath) {
  const bridgeDirectory = dirname(bridgeConfigPath);

  if (bridgeConfig?.emsApi?.baseUrl) {
    try {
      await probeHttpJson(`${bridgeConfig.emsApi.baseUrl}/health`, bridgeConfig.emsApi.timeoutMs ?? 5000);
      addPass(results, "Probe", "Bridge reached the EMS /health endpoint.");
    } catch (error) {
      addFail(results, "Probe", `Bridge could not reach EMS /health: ${String(error.message ?? error)}.`);
    }
  }

  if (bridgeConfig?.mqtt?.kind === "mqtt-tcp") {
    try {
      if (bridgeConfig.mqtt.tls?.enabled) {
        const tlsConfig = bridgeConfig.mqtt.tls.caCertPath
          ? {
              ...bridgeConfig.mqtt.tls,
              caCertPath: resolveMaybeRelative(bridgeDirectory, bridgeConfig.mqtt.tls.caCertPath)
            }
          : bridgeConfig.mqtt.tls;
        await probeTlsSocket(
          bridgeConfig.mqtt.host,
          bridgeConfig.mqtt.port,
          tlsConfig,
          5000
        );
      } else {
        await probeTcpSocket(bridgeConfig.mqtt.host, bridgeConfig.mqtt.port, 5000);
      }
      addPass(results, "Probe", "Bridge reached the MQTT endpoint.");
    } catch (error) {
      addFail(results, "Probe", `Bridge could not reach MQTT: ${String(error.message ?? error)}.`);
    }
  }

  if (bridgeConfig?.lte?.kind === "http-json" && bridgeConfig.lte.url) {
    try {
      await probeHttpJson(bridgeConfig.lte.url, bridgeConfig.lte.timeoutMs ?? 5000);
      addPass(results, "Probe", "Bridge reached the LTE modem HTTP endpoint.");
    } catch (error) {
      addFail(results, "Probe", `Bridge could not reach the LTE endpoint: ${String(error.message ?? error)}.`);
    }
  }

  if (emsConfig?.inverter?.kind === "modbus-tcp") {
    try {
      await probeTcpSocket(emsConfig.inverter.host, emsConfig.inverter.port, emsConfig.inverter.timeoutMs ?? 5000);
      addPass(results, "Probe", "EMS reached the Modbus TCP endpoint.");
    } catch (error) {
      addFail(results, "Probe", `EMS could not reach Modbus TCP: ${String(error.message ?? error)}.`);
    }
  }
}

function printResults(results) {
  const counts = {
    pass: results.filter((entry) => entry.level === "pass").length,
    warn: results.filter((entry) => entry.level === "warn").length,
    fail: results.filter((entry) => entry.level === "fail").length
  };

  for (const entry of results) {
    console.log(`[${entry.level}] ${entry.scope}: ${entry.message}`);
  }

  console.log(
    `Summary: ${String(counts.pass)} pass, ${String(counts.warn)} warn, ${String(counts.fail)} fail`
  );

  return counts;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  const emsConfigPath = resolve(options.emsConfigPath);
  const bridgeConfigPath = resolve(options.bridgeConfigPath);
  const emsConfig = resolveEnvPlaceholders(await readJsonFile(emsConfigPath));
  const bridgeConfig = resolveEnvPlaceholders(await readJsonFile(bridgeConfigPath));
  const results = [];

  await validateEmsConfig(results, emsConfig, emsConfigPath);
  await validateBridgeConfig(results, bridgeConfig, bridgeConfigPath);
  validateCrossConfig(results, emsConfig, bridgeConfig);

  if (options.probe) {
    await runProbes(results, emsConfig, bridgeConfig, bridgeConfigPath);
  }

  const counts = printResults(results);
  if (counts.fail > 0) {
    process.exitCode = 1;
  }
}

main().catch((error) => {
  console.error(`[fail] live-readiness: ${error instanceof Error ? error.message : String(error)}`);
  process.exitCode = 1;
});
