import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { spawn } from "node:child_process";
import { mkdir, readFile, rename, stat, writeFile } from "node:fs/promises";
import { dirname, isAbsolute, resolve } from "node:path";
import type {
  ClusterAlert,
  ClusterTelemetry,
  CommandAcknowledgement,
  OperationalEvent,
  RemoteCommand
} from "@clusterstore/contracts";
import { UtilityCoreBridgeService, type BufferedMessage, type ClockPort, type ClusterEmsApi, type CommandAuthorizerPort, type CommandLedgerPort, type LocalScadaPort, type LteModemPort, type OperationalJournalPort, type TelemetryBufferPort, type UtilityCoreBridgeConfig } from "./bridge-service.ts";
import { MqttTcpClient, type MqttTcpClientConfig } from "./mqtt-client.ts";

export interface CommandInvocationConfig {
  command: string;
  args?: string[];
  cwd?: string;
  env?: Record<string, string>;
  timeoutMs?: number;
}

export interface HttpClusterEmsApiConfig {
  baseUrl: string;
  timeoutMs?: number;
}

export interface FileTelemetryBufferConfig {
  kind: "file";
  path: string;
}

export interface FileScadaConfig {
  kind: "file";
  telemetryPath: string;
  alertsPath?: string;
}

export interface AllowAllAuthorizerConfig {
  kind: "allow-all";
}

export interface PolicyAuthorizerConfig {
  kind: "policy";
  allowedRoles?: string[];
  allowedRequesters?: string[];
}

export interface FileCommandLedgerConfig {
  kind: "file";
  path: string;
}

export interface JsonLineJournalConfig {
  kind: "jsonl-file";
  path: string;
}

export interface StateFileLteConfig {
  kind: "state-file";
  path: string;
}

export interface HttpJsonLteConfig {
  kind: "http-json";
  url: string;
  timeoutMs?: number;
}

export interface CommandLteConfig {
  kind: "command";
  query: CommandInvocationConfig;
}

export type UtilityCoreBridgeLteConfig =
  | StateFileLteConfig
  | HttpJsonLteConfig
  | CommandLteConfig;

export type UtilityCoreBridgeAuthorizerConfig =
  | AllowAllAuthorizerConfig
  | PolicyAuthorizerConfig;

export interface UtilityCoreBridgeDaemonConfig {
  bridge: UtilityCoreBridgeConfig;
  publish: {
    intervalMs: number;
    runOnStart?: boolean;
  };
  http: {
    host: string;
    port: number;
  };
  mqtt: MqttTcpClientConfig;
  lte: UtilityCoreBridgeLteConfig;
  emsApi: HttpClusterEmsApiConfig;
  buffer: FileTelemetryBufferConfig;
  scada: FileScadaConfig;
  authorizer: UtilityCoreBridgeAuthorizerConfig;
  commandLedger: FileCommandLedgerConfig;
  journal: JsonLineJournalConfig;
}

export interface UtilityCoreBridgeDaemonState {
  running: boolean;
  startedAt?: string;
  lastPublishAt?: string;
  lastSuccessAt?: string;
  lastError?: string;
  httpAddress?: string;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

async function ensureParentDirectory(filePath: string): Promise<void> {
  await mkdir(dirname(filePath), { recursive: true });
}

async function safeReadText(path: string): Promise<string> {
  try {
    return await readFile(path, "utf8");
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") {
      return "";
    }
    throw error;
  }
}

async function readJsonFile<T>(path: string, fallback: T): Promise<T> {
  try {
    const content = await readFile(path, "utf8");
    return JSON.parse(content) as T;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") {
      return fallback;
    }
    throw error;
  }
}

const ENV_PLACEHOLDER_PATTERN = /\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}/g;
const FULL_ENV_PLACEHOLDER_PATTERN = /^\$\{([A-Za-z_][A-Za-z0-9_]*)(:-([^}]*))?\}$/;

function resolveEnvPlaceholdersInString(template: string): unknown {
  let missingEnvironmentVariable: string | undefined;
  const resolved = template.replace(
    ENV_PLACEHOLDER_PATTERN,
    (_match, name: string, fallbackGroup: string | undefined, fallbackValue: string | undefined) => {
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
      `Missing required environment variable '${missingEnvironmentVariable}' while loading bridge config.`
    );
  }

  if (FULL_ENV_PLACEHOLDER_PATTERN.test(template)) {
    try {
      return JSON.parse(resolved) as unknown;
    } catch {
      return resolved;
    }
  }

  return resolved;
}

function resolveEnvPlaceholders<T>(value: T): T {
  if (typeof value === "string") {
    return resolveEnvPlaceholdersInString(value) as T;
  }

  if (Array.isArray(value)) {
    return value.map((entry) => resolveEnvPlaceholders(entry)) as T;
  }

  if (typeof value === "object" && value !== null) {
    return Object.fromEntries(
      Object.entries(value).map(([key, entry]) => [
        key,
        resolveEnvPlaceholders(entry)
      ])
    ) as T;
  }

  return value;
}

async function writeJsonFileAtomic(path: string, value: unknown): Promise<void> {
  await ensureParentDirectory(path);
  const tempPath = `${path}.tmp`;
  await writeFile(tempPath, `${JSON.stringify(value, null, 2)}\n`, "utf8");
  await rename(tempPath, path);
}

async function appendJsonLine(path: string, value: unknown): Promise<void> {
  await ensureParentDirectory(path);
  const existing = await safeReadText(path);
  const content =
    existing.length === 0
      ? `${JSON.stringify(value)}\n`
      : `${existing}${JSON.stringify(value)}\n`;
  await writeFile(path, content, "utf8");
}

async function invokeJsonCommand<TInput, TOutput>(
  config: CommandInvocationConfig,
  input?: TInput
): Promise<TOutput> {
  return new Promise<TOutput>((resolvePromise, rejectPromise) => {
    const child = spawn(config.command, config.args ?? [], {
      cwd: config.cwd,
      env: {
        ...process.env,
        ...config.env
      },
      stdio: ["pipe", "pipe", "pipe"]
    });

    let stdout = "";
    let stderr = "";
    const timeoutHandle = setTimeout(() => {
      child.kill();
      rejectPromise(
        new Error(
          `Command timed out after ${String(config.timeoutMs ?? 15_000)} ms: ${config.command}`
        )
      );
    }, config.timeoutMs ?? 15_000);

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk: string) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk: string) => {
      stderr += chunk;
    });
    child.on("error", (error) => {
      clearTimeout(timeoutHandle);
      rejectPromise(error);
    });
    child.on("close", (code) => {
      clearTimeout(timeoutHandle);
      if (code !== 0) {
        rejectPromise(
          new Error(
            `Command failed with exit code ${String(code)}: ${config.command}\n${stderr.trim()}`
          )
        );
        return;
      }

      const trimmed = stdout.trim();
      if (trimmed.length === 0) {
        resolvePromise(undefined as TOutput);
        return;
      }

      try {
        resolvePromise(JSON.parse(trimmed) as TOutput);
      } catch (error) {
        rejectPromise(
          new Error(
            `Command produced invalid JSON: ${config.command}\n${String(error)}\n${trimmed}`
          )
        );
      }
    });

    if (input === undefined) {
      child.stdin.end();
      return;
    }

    child.stdin.end(JSON.stringify(input));
  });
}

class SystemClock implements ClockPort {
  public now(): Date {
    return new Date();
  }
}

class HttpClusterEmsClient implements ClusterEmsApi {
  private readonly config: HttpClusterEmsApiConfig;

  public constructor(config: HttpClusterEmsApiConfig) {
    this.config = config;
  }

  public async getSnapshot(): Promise<ClusterTelemetry> {
    return this.request<ClusterTelemetry>("/snapshot");
  }

  public async drainAlerts(): Promise<ClusterAlert[]> {
    return this.request<ClusterAlert[]>("/alerts?drain=true");
  }

  public async applyRemoteCommand(
    command: RemoteCommand
  ): Promise<CommandAcknowledgement> {
    return this.request<CommandAcknowledgement>("/commands", {
      method: "POST",
      body: command
    });
  }

  private async request<T>(
    path: string,
    options: {
      method?: string;
      body?: unknown;
    } = {}
  ): Promise<T> {
    const controller = new AbortController();
    const timeout = setTimeout(() => {
      controller.abort();
    }, this.config.timeoutMs ?? 5_000);
    try {
      const response = await fetch(`${this.config.baseUrl}${path}`, {
        method: options.method ?? "GET",
        headers: {
          "content-type": "application/json"
        },
        body: options.body === undefined ? undefined : JSON.stringify(options.body),
        signal: controller.signal
      });
      if (!response.ok) {
        throw new Error(`EMS API request failed with status ${String(response.status)}.`);
      }
      return (await response.json()) as T;
    } finally {
      clearTimeout(timeout);
    }
  }
}

class FileTelemetryBuffer implements TelemetryBufferPort {
  private readonly path: string;

  public constructor(config: FileTelemetryBufferConfig) {
    this.path = config.path;
  }

  public async enqueue(message: BufferedMessage): Promise<void> {
    const pending = await readJsonFile<BufferedMessage[]>(this.path, []);
    if (!pending.some((entry) => entry.id === message.id)) {
      pending.push(message);
      await writeJsonFileAtomic(this.path, pending);
    }
  }

  public async peekPending(limit: number): Promise<BufferedMessage[]> {
    const pending = await readJsonFile<BufferedMessage[]>(this.path, []);
    return pending.slice(0, limit);
  }

  public async acknowledge(messageIds: string[]): Promise<void> {
    const pending = await readJsonFile<BufferedMessage[]>(this.path, []);
    await writeJsonFileAtomic(
      this.path,
      pending.filter((message) => !messageIds.includes(message.id))
    );
  }
}

class FileScadaPort implements LocalScadaPort {
  private readonly config: FileScadaConfig;

  public constructor(config: FileScadaConfig) {
    this.config = config;
  }

  public async publishTelemetry(snapshot: ClusterTelemetry): Promise<void> {
    await writeJsonFileAtomic(this.config.telemetryPath, snapshot);
  }

  public async publishAlerts(alerts: ClusterAlert[]): Promise<void> {
    if (!this.config.alertsPath || alerts.length === 0) {
      return;
    }

    for (const alert of alerts) {
      await appendJsonLine(this.config.alertsPath, alert);
    }
  }
}

class AllowAllAuthorizer implements CommandAuthorizerPort {
  public async authorize(): Promise<{ authorized: boolean; reason?: string }> {
    return { authorized: true };
  }
}

class PolicyAuthorizer implements CommandAuthorizerPort {
  private readonly config: PolicyAuthorizerConfig;

  public constructor(config: PolicyAuthorizerConfig) {
    this.config = config;
  }

  public async authorize(command: RemoteCommand): Promise<{ authorized: boolean; reason?: string }> {
    if (
      this.config.allowedRoles &&
      !this.config.allowedRoles.includes(command.authorization.role)
    ) {
      return {
        authorized: false,
        reason: "Command role is not in the bridge allow-list."
      };
    }

    if (
      this.config.allowedRequesters &&
      !this.config.allowedRequesters.includes(command.requestedBy)
    ) {
      return {
        authorized: false,
        reason: "Requester is not in the bridge allow-list."
      };
    }

    return { authorized: true };
  }
}

interface CommandLedgerState {
  seen: string[];
  received: RemoteCommand[];
  acknowledgements: CommandAcknowledgement[];
}

class FileCommandLedger implements CommandLedgerPort {
  private readonly path: string;

  public constructor(config: FileCommandLedgerConfig) {
    this.path = config.path;
  }

  public async hasSeen(idempotencyKey: string): Promise<boolean> {
    const state = await this.readState();
    return state.seen.includes(idempotencyKey);
  }

  public async recordReceived(command: RemoteCommand): Promise<void> {
    const state = await this.readState();
    state.received.push(command);
    await writeJsonFileAtomic(this.path, state);
  }

  public async recordAcknowledgement(
    acknowledgement: CommandAcknowledgement
  ): Promise<void> {
    const state = await this.readState();
    state.acknowledgements.push(acknowledgement);
    if (!state.seen.includes(acknowledgement.idempotencyKey)) {
      state.seen.push(acknowledgement.idempotencyKey);
    }
    await writeJsonFileAtomic(this.path, state);
  }

  private async readState(): Promise<CommandLedgerState> {
    return readJsonFile<CommandLedgerState>(this.path, {
      seen: [],
      received: [],
      acknowledgements: []
    });
  }
}

class JsonLineOperationalJournalPort implements OperationalJournalPort {
  private readonly path: string;

  public constructor(config: JsonLineJournalConfig) {
    this.path = config.path;
  }

  public async record(event: OperationalEvent): Promise<void> {
    await appendJsonLine(this.path, event);
  }
}

class StateFileLteModemPort implements LteModemPort {
  private readonly path: string;

  public constructor(config: StateFileLteConfig) {
    this.path = config.path;
  }

  public async isOnline(): Promise<boolean> {
    const state = await readJsonFile<Record<string, unknown>>(this.path, {
      online: true,
      rssi: -70
    });
    return extractOnlineState(state);
  }

  public async signalQualityRssi(): Promise<number> {
    const state = await readJsonFile<Record<string, unknown>>(this.path, {
      online: true,
      rssi: -70
    });
    return extractRssiValue(state);
  }
}

class HttpJsonLteModemPort implements LteModemPort {
  private readonly config: HttpJsonLteConfig;

  public constructor(config: HttpJsonLteConfig) {
    this.config = config;
  }

  public async isOnline(): Promise<boolean> {
    const state = await this.readState();
    return extractOnlineState(state);
  }

  public async signalQualityRssi(): Promise<number> {
    const state = await this.readState();
    return extractRssiValue(state);
  }

  private async readState(): Promise<Record<string, unknown>> {
    const controller = new AbortController();
    const timeout = setTimeout(() => {
      controller.abort();
    }, this.config.timeoutMs ?? 5_000);
    try {
      const response = await fetch(this.config.url, {
        signal: controller.signal
      });
      if (!response.ok) {
        throw new Error(`LTE status request failed with ${String(response.status)}.`);
      }
      return (await response.json()) as Record<string, unknown>;
    } finally {
      clearTimeout(timeout);
    }
  }
}

class CommandLteModemPort implements LteModemPort {
  private readonly config: CommandLteConfig;

  public constructor(config: CommandLteConfig) {
    this.config = config;
  }

  public async isOnline(): Promise<boolean> {
    const state = await invokeJsonCommand<undefined, Record<string, unknown>>(
      this.config.query
    );
    return extractOnlineState(state);
  }

  public async signalQualityRssi(): Promise<number> {
    const state = await invokeJsonCommand<undefined, Record<string, unknown>>(
      this.config.query
    );
    return extractRssiValue(state);
  }
}

function extractOnlineState(state: Record<string, unknown>): boolean {
  if ("online" in state) {
    return Boolean(state.online);
  }

  if ("connected" in state) {
    return Boolean(state.connected);
  }

  return false;
}

function extractRssiValue(state: Record<string, unknown>): number {
  const candidates = [
    state.rssi,
    state.signalRssiDbm,
    state.signalQualityRssi,
    state.rssiDbm
  ];

  for (const candidate of candidates) {
    if (candidate !== undefined && candidate !== null && candidate !== "") {
      return Number(candidate);
    }
  }

  return -70;
}

function createLtePort(config: UtilityCoreBridgeLteConfig): LteModemPort {
  switch (config.kind) {
    case "state-file":
      return new StateFileLteModemPort(config);
    case "http-json":
      return new HttpJsonLteModemPort(config);
    case "command":
      return new CommandLteModemPort(config);
  }
}

function createAuthorizer(
  config: UtilityCoreBridgeAuthorizerConfig
): CommandAuthorizerPort {
  switch (config.kind) {
    case "allow-all":
      return new AllowAllAuthorizer();
    case "policy":
      return new PolicyAuthorizer(config);
  }
}

async function readRequestBody(request: IncomingMessage): Promise<unknown> {
  const chunks: Buffer[] = [];
  for await (const chunk of request) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }

  if (chunks.length === 0) {
    return undefined;
  }

  return JSON.parse(Buffer.concat(chunks).toString("utf8")) as unknown;
}

function writeJsonResponse(response: ServerResponse, statusCode: number, body: unknown): void {
  response.statusCode = statusCode;
  response.setHeader("content-type", "application/json; charset=utf-8");
  response.end(`${JSON.stringify(body, null, 2)}\n`);
}

export class UtilityCoreBridgeDaemon {
  private readonly config: UtilityCoreBridgeDaemonConfig;
  private readonly state: UtilityCoreBridgeDaemonState = {
    running: false
  };
  private readonly clock: ClockPort = new SystemClock();
  private readonly mqttClient: MqttTcpClient;
  private readonly bridge: UtilityCoreBridgeService;
  private readonly server = createServer();
  private publishTimer?: NodeJS.Timeout;
  private cycleInFlight = false;

  public constructor(config: UtilityCoreBridgeDaemonConfig) {
    this.config = config;
    this.mqttClient = new MqttTcpClient(config.mqtt);
    this.bridge = new UtilityCoreBridgeService(
      config.bridge,
      this.clock,
      this.mqttClient,
      createLtePort(config.lte),
      new FileTelemetryBuffer(config.buffer),
      new FileScadaPort(config.scada),
      createAuthorizer(config.authorizer),
      new FileCommandLedger(config.commandLedger),
      new JsonLineOperationalJournalPort(config.journal),
      new HttpClusterEmsClient(config.emsApi)
    );
    this.server.on("request", async (request, response) => {
      await this.handleRequest(request, response);
    });
  }

  public snapshotState(): UtilityCoreBridgeDaemonState {
    return { ...this.state };
  }

  public async start(options: { once?: boolean } = {}): Promise<void> {
    if (this.state.running) {
      return;
    }

    this.state.running = true;
    this.state.startedAt = this.clock.now().toISOString();
    await new Promise<void>((resolvePromise, rejectPromise) => {
      this.server.once("error", rejectPromise);
      this.server.listen(this.config.http.port, this.config.http.host, () => {
        this.server.off("error", rejectPromise);
        resolvePromise();
      });
    });
    const address = this.server.address();
    const resolvedPort =
      typeof address === "object" && address !== null
        ? address.port
        : this.config.http.port;
    this.state.httpAddress = `http://${this.config.http.host}:${String(resolvedPort)}`;

    await this.bridge.bindCommandSubscription();
    if (this.config.publish.runOnStart ?? true) {
      await this.publishCycle();
    }

    if (options.once) {
      await this.stop();
      return;
    }

    this.scheduleNextCycle();
  }

  public async stop(): Promise<void> {
    this.state.running = false;
    if (this.publishTimer) {
      clearTimeout(this.publishTimer);
      this.publishTimer = undefined;
    }

    await new Promise<void>((resolvePromise) => {
      this.server.close(() => {
        resolvePromise();
      });
    });
    await this.mqttClient.close();
  }

  public async publishCycle(): Promise<void> {
    this.state.lastPublishAt = this.clock.now().toISOString();
    this.cycleInFlight = true;
    try {
      await this.bridge.publishCycle();
      this.state.lastSuccessAt = this.clock.now().toISOString();
      this.state.lastError = undefined;
    } catch (error) {
      this.state.lastError = error instanceof Error ? error.message : String(error);
      throw error;
    } finally {
      this.cycleInFlight = false;
    }
  }

  private scheduleNextCycle(): void {
    if (!this.state.running) {
      return;
    }

    this.publishTimer = setTimeout(async () => {
      if (!this.cycleInFlight) {
        try {
          await this.publishCycle();
        } catch (error) {
          console.error("[Bridge] publish cycle failed", error);
        }
      }
      this.scheduleNextCycle();
    }, this.config.publish.intervalMs);
  }

  private async handleRequest(
    request: IncomingMessage,
    response: ServerResponse
  ): Promise<void> {
    try {
      const url = new URL(request.url ?? "/", "http://clusterstore.local");
      const method = request.method ?? "GET";

      if (method === "GET" && url.pathname === "/health") {
        writeJsonResponse(response, 200, this.snapshotState());
        return;
      }

      if (method === "POST" && url.pathname === "/publish-cycle") {
        await this.publishCycle();
        writeJsonResponse(response, 200, {
          ok: true,
          lastSuccessAt: this.state.lastSuccessAt
        });
        return;
      }

      if (method === "POST" && url.pathname === "/ems-command") {
        const body = await readRequestBody(request);
        writeJsonResponse(
          response,
          200,
          await new HttpClusterEmsClient(this.config.emsApi).applyRemoteCommand(
            body as RemoteCommand
          )
        );
        return;
      }

      writeJsonResponse(response, 404, {
        error: "Not found",
        method,
        path: url.pathname
      });
    } catch (error) {
      writeJsonResponse(response, 500, {
        error: error instanceof Error ? error.message : String(error)
      });
    }
  }
}

function resolvePathMaybeRelative(configDirectory: string, path: string | undefined): string | undefined {
  if (!path) {
    return path;
  }

  return isAbsolute(path) ? path : resolve(configDirectory, path);
}

function resolveCommandConfig(
  configDirectory: string,
  config: CommandInvocationConfig
): CommandInvocationConfig {
  return {
    ...config,
    cwd: resolvePathMaybeRelative(configDirectory, config.cwd)
  };
}

export function normalizeUtilityCoreBridgeDaemonConfig(
  configDirectory: string,
  config: UtilityCoreBridgeDaemonConfig
): UtilityCoreBridgeDaemonConfig {
  return {
    ...config,
    emsApi: {
      ...config.emsApi,
      baseUrl: config.emsApi.baseUrl
    },
    mqtt: config.mqtt.tls?.caCertPath
      ? {
          ...config.mqtt,
          tls: {
            ...config.mqtt.tls,
            caCertPath: resolvePathMaybeRelative(
              configDirectory,
              config.mqtt.tls.caCertPath
            )
          }
        }
      : config.mqtt,
    lte: (() => {
      switch (config.lte.kind) {
        case "state-file":
          return {
            ...config.lte,
            path: resolvePathMaybeRelative(configDirectory, config.lte.path) ?? config.lte.path
          } satisfies StateFileLteConfig;
        case "http-json":
          return config.lte;
        case "command":
          return {
            ...config.lte,
            query: resolveCommandConfig(configDirectory, config.lte.query)
          } satisfies CommandLteConfig;
      }
    })(),
    buffer: {
      ...config.buffer,
      path: resolvePathMaybeRelative(configDirectory, config.buffer.path) ?? config.buffer.path
    },
    scada: {
      ...config.scada,
      telemetryPath:
        resolvePathMaybeRelative(configDirectory, config.scada.telemetryPath) ??
        config.scada.telemetryPath,
      alertsPath: resolvePathMaybeRelative(configDirectory, config.scada.alertsPath)
    },
    commandLedger: {
      ...config.commandLedger,
      path:
        resolvePathMaybeRelative(configDirectory, config.commandLedger.path) ??
        config.commandLedger.path
    },
    journal: {
      ...config.journal,
      path: resolvePathMaybeRelative(configDirectory, config.journal.path) ?? config.journal.path
    }
  };
}

export async function loadUtilityCoreBridgeDaemonConfig(
  configPath: string
): Promise<UtilityCoreBridgeDaemonConfig> {
  const config = await readJsonFile<UtilityCoreBridgeDaemonConfig | null>(configPath, null);
  if (!config) {
    throw new Error(`Bridge config file not found: ${configPath}`);
  }

  return normalizeUtilityCoreBridgeDaemonConfig(
    dirname(configPath),
    resolveEnvPlaceholders(config)
  );
}

export function parseUtilityCoreBridgeCliOptions(argv: string[]): {
  configPath: string;
  once: boolean;
} {
  let configPath = "";
  let once = false;

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if ((argument === "--config" || argument === "-c") && argv[index + 1]) {
      configPath = argv[index + 1] ?? "";
      index += 1;
      continue;
    }
    if (argument === "--once") {
      once = true;
    }
  }

  if (!configPath) {
    throw new Error("Expected --config <path>.");
  }

  return {
    configPath,
    once
  };
}
