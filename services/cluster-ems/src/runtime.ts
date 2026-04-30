import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { spawn } from "node:child_process";
import { Socket } from "node:net";
import {
  mkdir,
  readFile,
  rename,
  rm,
  stat,
  writeFile
} from "node:fs/promises";
import { dirname, isAbsolute, resolve } from "node:path";
import type {
  ClusterAlert,
  ClusterTelemetry,
  GridInverterState,
  InverterSetpoint,
  NodeCommandFrame,
  NodeDiagnosticFrame,
  NodeStatusFrame,
  OperationalEvent,
  RemoteCommand,
  TariffBand
} from "@clusterstore/contracts";
import { ClusterEmsController, type ClockPort } from "./ems-controller.ts";
import type { ClusterEmsConfig } from "./config.ts";
import type { CanBusPort } from "./adapters/can-bus.ts";
import type { GridInverterPort } from "./adapters/modbus.ts";
import type { HmiPort } from "./adapters/hmi.ts";
import type { OperationalJournalPort } from "./adapters/journal.ts";
import type { WatchdogPort } from "./adapters/watchdog.ts";
import {
  OverlayBmsAdapter,
  type OverlayAssetTelemetry,
  type OverlayBmsAssetPort,
  type OverlayDispatchRequest
} from "./adapters/bms-adapter.ts";

export interface CommandInvocationConfig {
  command: string;
  args?: string[];
  cwd?: string;
  env?: Record<string, string>;
  timeoutMs?: number;
}

export interface JsonFileCanBusConfig {
  kind: "state-file";
  statusesPath: string;
  diagnosticsPath?: string;
  commandsPath: string;
  commandHistoryPath?: string;
  isolatesPath?: string;
}

export interface OverlayFileCanBusConfig {
  kind: "overlay-file";
  assetsPath: string;
  dispatchPath: string;
  dispatchHistoryPath?: string;
  isolatesPath?: string;
}

export interface CommandCanBusConfig {
  kind: "command";
  readStatuses: CommandInvocationConfig;
  readDiagnostics?: CommandInvocationConfig;
  writeCommands: CommandInvocationConfig;
  isolateNode: CommandInvocationConfig;
}

export type ClusterEmsCanBusConfig =
  | JsonFileCanBusConfig
  | OverlayFileCanBusConfig
  | CommandCanBusConfig;

export interface StateFileGridInverterConfig {
  kind: "state-file";
  statePath: string;
  setpointPath: string;
  setpointHistoryPath?: string;
  prechargePath?: string;
  holdOpenPath?: string;
}

export interface NumericRegisterFieldConfig {
  address: number;
  type: "u16" | "i16" | "u32" | "i32";
  scale?: number;
  wordOrder?: "msw-first" | "lsw-first";
}

export interface BooleanRegisterFieldConfig {
  address: number;
  type: "bool";
  trueValues?: number[];
}

export interface TariffRegisterFieldConfig {
  address: number;
  type: "tariff-band";
  values: Record<string, TariffBand>;
  defaultValue?: TariffBand;
}

export type RegisterFieldConfig =
  | NumericRegisterFieldConfig
  | BooleanRegisterFieldConfig
  | TariffRegisterFieldConfig;

export interface WritableRegisterFieldConfig extends NumericRegisterFieldConfig {
  writeFunction?: "single";
}

export interface WritableBooleanRegisterFieldConfig {
  address: number;
  type: "bool";
  trueValue?: number;
  falseValue?: number;
}

export interface WritableEnumRegisterFieldConfig {
  address: number;
  type: "enum";
  values: Record<string, number>;
}

export type WritableFieldConfig =
  | WritableRegisterFieldConfig
  | WritableBooleanRegisterFieldConfig
  | WritableEnumRegisterFieldConfig;

export interface ModbusTcpGridInverterConfig {
  kind: "modbus-tcp";
  host: string;
  port: number;
  unitId: number;
  timeoutMs?: number;
  stateMap: {
    acInputVoltageV: RegisterFieldConfig;
    acInputFrequencyHz: RegisterFieldConfig;
    acOutputVoltageV: RegisterFieldConfig;
    acOutputFrequencyHz: RegisterFieldConfig;
    acOutputLoadW: RegisterFieldConfig;
    dcBusVoltageV: RegisterFieldConfig;
    gridAvailable: RegisterFieldConfig;
    solarGenerationW: RegisterFieldConfig;
    availableChargeCurrentA: RegisterFieldConfig;
    requestedDischargeCurrentA: RegisterFieldConfig;
    exportAllowed: RegisterFieldConfig;
    tariffBand: RegisterFieldConfig;
    meteredSitePowerW?: RegisterFieldConfig;
  };
  setpointMap?: {
    operatingMode?: WritableFieldConfig;
    aggregateChargeCurrentA?: WritableFieldConfig;
    aggregateDischargeCurrentA?: WritableFieldConfig;
    exportLimitW?: WritableFieldConfig;
    prechargeTargetVoltageV?: WritableFieldConfig;
    holdOpenBus?: WritableFieldConfig;
  };
}

export interface CommandGridInverterConfig {
  kind: "command";
  readState: CommandInvocationConfig;
  writeSetpoint: CommandInvocationConfig;
  prechargeDcBus: CommandInvocationConfig;
  holdOpenBus: CommandInvocationConfig;
}

export type ClusterEmsGridInverterConfig =
  | StateFileGridInverterConfig
  | ModbusTcpGridInverterConfig
  | CommandGridInverterConfig;

export interface ConsoleHmiConfig {
  kind: "console";
}

export interface FileHmiConfig {
  kind: "file";
  snapshotPath: string;
  alertsPath?: string;
}

export type ClusterEmsHmiConfig = ConsoleHmiConfig | FileHmiConfig;

export interface FileWatchdogConfig {
  kind: "file";
  heartbeatPath: string;
  failSafePath?: string;
}

export interface CommandWatchdogConfig {
  kind: "command";
  kick: CommandInvocationConfig;
  triggerFailSafe: CommandInvocationConfig;
}

export type ClusterEmsWatchdogConfig =
  | FileWatchdogConfig
  | CommandWatchdogConfig;

export interface JsonLineJournalConfig {
  kind: "jsonl-file";
  path: string;
}

export interface CommandJournalConfig {
  kind: "command";
  record: CommandInvocationConfig;
}

export type ClusterEmsJournalConfig =
  | JsonLineJournalConfig
  | CommandJournalConfig;

export interface ClusterEmsDaemonConfig {
  config: ClusterEmsConfig;
  cycle: {
    intervalMs: number;
    runOnStart?: boolean;
  };
  http: {
    host: string;
    port: number;
  };
  canBus: ClusterEmsCanBusConfig;
  inverter: ClusterEmsGridInverterConfig;
  hmi: ClusterEmsHmiConfig;
  watchdog: ClusterEmsWatchdogConfig;
  journal: ClusterEmsJournalConfig;
}

export interface ClusterEmsDaemonState {
  running: boolean;
  startedAt?: string;
  lastCycleAt?: string;
  lastSuccessAt?: string;
  lastError?: string;
  httpAddress?: string;
}

export interface ClusterEmsDaemonStartOptions {
  once?: boolean;
}

interface HttpResponseBody {
  statusCode: number;
  body: unknown;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function createDefaultGridInverterState(): GridInverterState {
  return {
    acInputVoltageV: 230,
    acInputFrequencyHz: 50,
    acOutputVoltageV: 230,
    acOutputFrequencyHz: 50,
    acOutputLoadW: 0,
    dcBusVoltageV: 51.2,
    gridAvailable: true,
    solarGenerationW: 0,
    availableChargeCurrentA: 0,
    requestedDischargeCurrentA: 0,
    exportAllowed: false,
    tariffBand: "normal"
  };
}

async function ensureParentDirectory(filePath: string): Promise<void> {
  await mkdir(dirname(filePath), { recursive: true });
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
  const nextContent = existing.length === 0
    ? `${JSON.stringify(value)}\n`
    : `${existing}${JSON.stringify(value)}\n`;
  await writeFile(path, nextContent, "utf8");
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
      `Missing required environment variable '${missingEnvironmentVariable}' while loading EMS config.`
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

async function fileExists(path: string): Promise<boolean> {
  try {
    await stat(path);
    return true;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") {
      return false;
    }
    throw error;
  }
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
    let resolved = false;
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
      if (resolved) {
        return;
      }

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
        resolved = true;
        return;
      }

      try {
        resolvePromise(JSON.parse(trimmed) as TOutput);
        resolved = true;
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

class JsonFileCanBusPort implements CanBusPort {
  private readonly config: JsonFileCanBusConfig;

  public constructor(config: JsonFileCanBusConfig) {
    this.config = config;
  }

  public async readStatuses(): Promise<NodeStatusFrame[]> {
    return readJsonFile<NodeStatusFrame[]>(this.config.statusesPath, []);
  }

  public async readDiagnostics(): Promise<NodeDiagnosticFrame[]> {
    if (!this.config.diagnosticsPath) {
      return [];
    }

    return readJsonFile<NodeDiagnosticFrame[]>(this.config.diagnosticsPath, []);
  }

  public async writeCommands(commands: NodeCommandFrame[]): Promise<void> {
    await writeJsonFileAtomic(this.config.commandsPath, commands);
    if (this.config.commandHistoryPath) {
      await appendJsonLine(this.config.commandHistoryPath, {
        timestamp: new Date().toISOString(),
        commands
      });
    }
  }

  public async isolateNode(nodeId: string): Promise<void> {
    if (!this.config.isolatesPath) {
      return;
    }

    await appendJsonLine(this.config.isolatesPath, {
      timestamp: new Date().toISOString(),
      nodeId
    });
  }
}

class OverlayFileAssetPort implements OverlayBmsAssetPort {
  private readonly config: OverlayFileCanBusConfig;

  public constructor(config: OverlayFileCanBusConfig) {
    this.config = config;
  }

  public async readAssets(): Promise<OverlayAssetTelemetry[]> {
    return readJsonFile<OverlayAssetTelemetry[]>(this.config.assetsPath, []);
  }

  public async writeDispatchRequests(requests: OverlayDispatchRequest[]): Promise<void> {
    await writeJsonFileAtomic(this.config.dispatchPath, requests);
    if (this.config.dispatchHistoryPath) {
      await appendJsonLine(this.config.dispatchHistoryPath, {
        timestamp: new Date().toISOString(),
        requests
      });
    }
  }

  public async isolateAsset(assetId: string): Promise<void> {
    if (!this.config.isolatesPath) {
      return;
    }

    await appendJsonLine(this.config.isolatesPath, {
      timestamp: new Date().toISOString(),
      assetId
    });
  }
}

class CommandCanBusPort implements CanBusPort {
  private readonly config: CommandCanBusConfig;

  public constructor(config: CommandCanBusConfig) {
    this.config = config;
  }

  public async readStatuses(): Promise<NodeStatusFrame[]> {
    return invokeJsonCommand<undefined, NodeStatusFrame[]>(
      this.config.readStatuses
    );
  }

  public async readDiagnostics(): Promise<NodeDiagnosticFrame[]> {
    if (!this.config.readDiagnostics) {
      return [];
    }

    return invokeJsonCommand<undefined, NodeDiagnosticFrame[]>(
      this.config.readDiagnostics
    );
  }

  public async writeCommands(commands: NodeCommandFrame[]): Promise<void> {
    await invokeJsonCommand<NodeCommandFrame[], void>(
      this.config.writeCommands,
      commands
    );
  }

  public async isolateNode(nodeId: string): Promise<void> {
    await invokeJsonCommand<{ nodeId: string }, void>(this.config.isolateNode, {
      nodeId
    });
  }
}

class StateFileGridInverterPort implements GridInverterPort {
  private readonly config: StateFileGridInverterConfig;

  public constructor(config: StateFileGridInverterConfig) {
    this.config = config;
  }

  public async readState(): Promise<GridInverterState> {
    return readJsonFile<GridInverterState>(
      this.config.statePath,
      createDefaultGridInverterState()
    );
  }

  public async writeSetpoint(setpoint: InverterSetpoint): Promise<void> {
    await writeJsonFileAtomic(this.config.setpointPath, setpoint);
    if (this.config.setpointHistoryPath) {
      await appendJsonLine(this.config.setpointHistoryPath, {
        timestamp: new Date().toISOString(),
        setpoint
      });
    }
  }

  public async prechargeDcBus(targetVoltageV: number): Promise<void> {
    if (this.config.prechargePath) {
      await appendJsonLine(this.config.prechargePath, {
        timestamp: new Date().toISOString(),
        targetVoltageV
      });
    }

    const state = await this.readState();
    await writeJsonFileAtomic(this.config.statePath, {
      ...state,
      dcBusVoltageV: targetVoltageV
    });
  }

  public async holdOpenBus(): Promise<void> {
    if (this.config.holdOpenPath) {
      await appendJsonLine(this.config.holdOpenPath, {
        timestamp: new Date().toISOString(),
        action: "hold_open"
      });
    }
  }
}

export class ModbusTcpGridInverterPort implements GridInverterPort {
  private readonly config: ModbusTcpGridInverterConfig;
  private transactionId = 0;

  public constructor(config: ModbusTcpGridInverterConfig) {
    this.config = config;
  }

  public async readState(): Promise<GridInverterState> {
    return {
      acInputVoltageV: Number(await this.readField(this.config.stateMap.acInputVoltageV)),
      acInputFrequencyHz: Number(
        await this.readField(this.config.stateMap.acInputFrequencyHz)
      ),
      acOutputVoltageV: Number(await this.readField(this.config.stateMap.acOutputVoltageV)),
      acOutputFrequencyHz: Number(
        await this.readField(this.config.stateMap.acOutputFrequencyHz)
      ),
      acOutputLoadW: Number(await this.readField(this.config.stateMap.acOutputLoadW)),
      dcBusVoltageV: Number(await this.readField(this.config.stateMap.dcBusVoltageV)),
      gridAvailable: Boolean(await this.readField(this.config.stateMap.gridAvailable)),
      solarGenerationW: Number(await this.readField(this.config.stateMap.solarGenerationW)),
      availableChargeCurrentA: Number(
        await this.readField(this.config.stateMap.availableChargeCurrentA)
      ),
      requestedDischargeCurrentA: Number(
        await this.readField(this.config.stateMap.requestedDischargeCurrentA)
      ),
      exportAllowed: Boolean(await this.readField(this.config.stateMap.exportAllowed)),
      tariffBand: (await this.readField(this.config.stateMap.tariffBand)) as TariffBand,
      meteredSitePowerW: this.config.stateMap.meteredSitePowerW
        ? Number(await this.readField(this.config.stateMap.meteredSitePowerW))
        : undefined
    };
  }

  public async writeSetpoint(setpoint: InverterSetpoint): Promise<void> {
    if (!this.config.setpointMap) {
      return;
    }

    const operations: Array<Promise<void>> = [];
    if (this.config.setpointMap.operatingMode) {
      operations.push(
        this.writeField(this.config.setpointMap.operatingMode, setpoint.operatingMode)
      );
    }
    if (this.config.setpointMap.aggregateChargeCurrentA) {
      operations.push(
        this.writeField(
          this.config.setpointMap.aggregateChargeCurrentA,
          setpoint.aggregateChargeCurrentA
        )
      );
    }
    if (this.config.setpointMap.aggregateDischargeCurrentA) {
      operations.push(
        this.writeField(
          this.config.setpointMap.aggregateDischargeCurrentA,
          setpoint.aggregateDischargeCurrentA
        )
      );
    }
    if (this.config.setpointMap.exportLimitW) {
      operations.push(
        this.writeField(this.config.setpointMap.exportLimitW, setpoint.exportLimitW)
      );
    }

    await Promise.all(operations);
  }

  public async prechargeDcBus(targetVoltageV: number): Promise<void> {
    if (!this.config.setpointMap?.prechargeTargetVoltageV) {
      return;
    }

    await this.writeField(
      this.config.setpointMap.prechargeTargetVoltageV,
      targetVoltageV
    );
  }

  public async holdOpenBus(): Promise<void> {
    if (!this.config.setpointMap?.holdOpenBus) {
      return;
    }

    await this.writeField(this.config.setpointMap.holdOpenBus, true);
  }

  private async readField(config: RegisterFieldConfig): Promise<number | boolean | TariffBand> {
    const words = await this.readHoldingRegisters(config.address, config.type === "u32" || config.type === "i32" ? 2 : 1);
    if (config.type === "bool") {
      const truthy = config.trueValues ?? [1];
      return truthy.includes(words[0] ?? 0);
    }

    if (config.type === "tariff-band") {
      const raw = words[0] ?? 0;
      const value = config.values[String(raw)];
      return value ?? config.defaultValue ?? "unavailable";
    }

    const numericValue = decodeNumericWords(words, config);
    return config.scale ? numericValue / config.scale : numericValue;
  }

  private async writeField(
    config: WritableFieldConfig,
    value: boolean | number | string
  ): Promise<void> {
    if (config.type === "enum") {
      const nextValue = config.values[String(value)];
      if (nextValue === undefined) {
        throw new Error(`No Modbus enum mapping for value ${String(value)}.`);
      }
      await this.writeSingleRegister(config.address, nextValue);
      return;
    }

    if (config.type === "bool") {
      await this.writeSingleRegister(
        config.address,
        value ? (config.trueValue ?? 1) : (config.falseValue ?? 0)
      );
      return;
    }

    const scaledValue = config.scale ? Number(value) * config.scale : Number(value);
    if (config.type === "u32" || config.type === "i32") {
      await this.writeMultipleRegisters(
        config.address,
        encode32BitRegisterWords(Math.round(scaledValue), config)
      );
      return;
    }

    await this.writeSingleRegister(
      config.address,
      encodeNumericRegisterValue(Math.round(scaledValue), config)
    );
  }

  private async readHoldingRegisters(address: number, count: number): Promise<number[]> {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x03, 0);
    pdu.writeUInt16BE(address, 1);
    pdu.writeUInt16BE(count, 3);
    const response = await this.sendRequest(pdu);
    if (response.readUInt8(0) !== 0x03) {
      throw new Error(`Unexpected Modbus function code: ${String(response.readUInt8(0))}`);
    }

    const byteCount = response.readUInt8(1);
    const words: number[] = [];
    for (let offset = 0; offset < byteCount; offset += 2) {
      words.push(response.readUInt16BE(2 + offset));
    }

    return words;
  }

  private async writeSingleRegister(address: number, value: number): Promise<void> {
    const pdu = Buffer.alloc(5);
    pdu.writeUInt8(0x06, 0);
    pdu.writeUInt16BE(address, 1);
    pdu.writeUInt16BE(value & 0xffff, 3);
    const response = await this.sendRequest(pdu);
    if (response.readUInt8(0) !== 0x06) {
      throw new Error(`Unexpected Modbus write response: ${String(response.readUInt8(0))}`);
    }
  }

  private async writeMultipleRegisters(
    address: number,
    values: readonly number[]
  ): Promise<void> {
    const byteCount = values.length * 2;
    const pdu = Buffer.alloc(6 + byteCount);
    pdu.writeUInt8(0x10, 0);
    pdu.writeUInt16BE(address, 1);
    pdu.writeUInt16BE(values.length, 3);
    pdu.writeUInt8(byteCount, 5);
    for (let i = 0; i < values.length; i++) {
      pdu.writeUInt16BE((values[i] ?? 0) & 0xffff, 6 + i * 2);
    }
    const response = await this.sendRequest(pdu);
    if (response.readUInt8(0) !== 0x10) {
      throw new Error(`Unexpected Modbus write-multiple response: ${String(response.readUInt8(0))}`);
    }
  }

  private async sendRequest(pdu: Buffer): Promise<Buffer> {
    const transactionId = (this.transactionId + 1) % 0xffff;
    this.transactionId = transactionId;

    return new Promise<Buffer>((resolvePromise, rejectPromise) => {
      const socket = new Socket();
      const timeoutMs = this.config.timeoutMs ?? 5_000;
      let settled = false;
      const chunks: Buffer[] = [];

      const finalize = (callback: () => void): void => {
        if (settled) {
          return;
        }

        settled = true;
        socket.destroy();
        callback();
      };

      socket.setTimeout(timeoutMs);
      socket.once("timeout", () => {
        finalize(() => {
          rejectPromise(
            new Error(
              `Timed out waiting for Modbus response from ${this.config.host}:${String(this.config.port)}`
            )
          );
        });
      });
      socket.once("error", (error) => {
        finalize(() => {
          rejectPromise(error);
        });
      });
      socket.on("data", (chunk) => {
        chunks.push(chunk);
        const packet = Buffer.concat(chunks);
        if (packet.byteLength < 7) {
          return;
        }

        const length = packet.readUInt16BE(4);
        const frameLength = 6 + length;
        if (packet.byteLength < frameLength) {
          return;
        }

        const responseTransactionId = packet.readUInt16BE(0);
        if (responseTransactionId !== transactionId) {
          return;
        }

        const functionCode = packet.readUInt8(7);
        if ((functionCode & 0x80) !== 0) {
          const exceptionCode = packet.readUInt8(8);
          finalize(() => {
            rejectPromise(
              new Error(
                `Modbus exception: ${modbusExceptionName(exceptionCode)} (0x${exceptionCode.toString(16).padStart(2, "0")}) for function 0x${(functionCode & 0x7f).toString(16).padStart(2, "0")}`
              )
            );
          });
          return;
        }

        const payload = packet.subarray(7, frameLength);
        finalize(() => {
          resolvePromise(payload);
        });
      });
      socket.connect(this.config.port, this.config.host, () => {
        const frame = Buffer.alloc(7 + pdu.byteLength);
        frame.writeUInt16BE(transactionId, 0);
        frame.writeUInt16BE(0, 2);
        frame.writeUInt16BE(pdu.byteLength + 1, 4);
        frame.writeUInt8(this.config.unitId, 6);
        pdu.copy(frame, 7);
        socket.write(frame);
      });
    });
  }
}

function modbusExceptionName(code: number): string {
  const names: Record<number, string> = {
    0x01: "Illegal Function",
    0x02: "Illegal Data Address",
    0x03: "Illegal Data Value",
    0x04: "Server Device Failure",
    0x05: "Acknowledge",
    0x06: "Server Device Busy",
    0x08: "Memory Parity Error",
    0x0a: "Gateway Path Unavailable",
    0x0b: "Gateway Target Device Failed to Respond"
  };
  return names[code] ?? "Unknown Exception";
}

function decodeNumericWords(
  words: number[],
  config: NumericRegisterFieldConfig
): number {
  if (config.type === "u16") {
    return words[0] ?? 0;
  }

  if (config.type === "i16") {
    const value = words[0] ?? 0;
    return value > 0x7fff ? value - 0x10000 : value;
  }

  const orderedWords =
    config.wordOrder === "lsw-first" ? [...words].reverse() : words;
  const combined = ((orderedWords[0] ?? 0) << 16) | (orderedWords[1] ?? 0);
  if (config.type === "u32") {
    return combined >>> 0;
  }

  return combined > 0x7fffffff ? combined - 0x1_0000_0000 : combined;
}

function encodeNumericRegisterValue(
  value: number,
  config: NumericRegisterFieldConfig
): number {
  if (config.type === "i16" && value < 0) {
    return 0x10000 + value;
  }

  return value;
}

function encode32BitRegisterWords(
  value: number,
  config: NumericRegisterFieldConfig
): [number, number] {
  const u32 = config.type === "i32" ? (value | 0) >>> 0 : value >>> 0;
  const msw = (u32 >>> 16) & 0xffff;
  const lsw = u32 & 0xffff;
  return config.wordOrder === "lsw-first" ? [lsw, msw] : [msw, lsw];
}

class CommandGridInverterPort implements GridInverterPort {
  private readonly config: CommandGridInverterConfig;

  public constructor(config: CommandGridInverterConfig) {
    this.config = config;
  }

  public async readState(): Promise<GridInverterState> {
    return invokeJsonCommand<undefined, GridInverterState>(this.config.readState);
  }

  public async writeSetpoint(setpoint: InverterSetpoint): Promise<void> {
    await invokeJsonCommand<InverterSetpoint, void>(this.config.writeSetpoint, setpoint);
  }

  public async prechargeDcBus(targetVoltageV: number): Promise<void> {
    await invokeJsonCommand<{ targetVoltageV: number }, void>(
      this.config.prechargeDcBus,
      { targetVoltageV }
    );
  }

  public async holdOpenBus(): Promise<void> {
    await invokeJsonCommand<{ action: string }, void>(this.config.holdOpenBus, {
      action: "hold_open"
    });
  }
}

class ConsoleHmiPort implements HmiPort {
  public async render(snapshot: ClusterTelemetry, alerts: ClusterAlert[]): Promise<void> {
    const summary = [
      `[EMS] ${snapshot.timestamp}`,
      `mode=${snapshot.clusterMode}`,
      `soc=${snapshot.aggregateSocPct.toFixed(2)}%`,
      `fresh=${String(snapshot.freshNodeCount)}`,
      `stale=${String(snapshot.staleNodeCount)}`,
      `alerts=${String(alerts.length)}`
    ].join(" ");
    console.log(summary);
  }
}

class FileHmiPort implements HmiPort {
  private readonly config: FileHmiConfig;

  public constructor(config: FileHmiConfig) {
    this.config = config;
  }

  public async render(snapshot: ClusterTelemetry, alerts: ClusterAlert[]): Promise<void> {
    await writeJsonFileAtomic(this.config.snapshotPath, snapshot);
    if (!this.config.alertsPath || alerts.length === 0) {
      return;
    }

    for (const alert of alerts) {
      await appendJsonLine(this.config.alertsPath, alert);
    }
  }
}

class FileWatchdogPort implements WatchdogPort {
  private readonly config: FileWatchdogConfig;

  public constructor(config: FileWatchdogConfig) {
    this.config = config;
  }

  public async kick(): Promise<void> {
    await writeJsonFileAtomic(this.config.heartbeatPath, {
      timestamp: new Date().toISOString(),
      ok: true
    });
  }

  public async triggerFailSafe(reason: string): Promise<void> {
    if (!this.config.failSafePath) {
      return;
    }

    await appendJsonLine(this.config.failSafePath, {
      timestamp: new Date().toISOString(),
      reason
    });
  }
}

class CommandWatchdogPort implements WatchdogPort {
  private readonly config: CommandWatchdogConfig;

  public constructor(config: CommandWatchdogConfig) {
    this.config = config;
  }

  public async kick(): Promise<void> {
    await invokeJsonCommand<undefined, void>(this.config.kick);
  }

  public async triggerFailSafe(reason: string): Promise<void> {
    await invokeJsonCommand<{ reason: string }, void>(this.config.triggerFailSafe, {
      reason
    });
  }
}

class JsonLineOperationalJournalPort implements OperationalJournalPort {
  private readonly config: JsonLineJournalConfig;

  public constructor(config: JsonLineJournalConfig) {
    this.config = config;
  }

  public async record(event: OperationalEvent): Promise<void> {
    await appendJsonLine(this.config.path, event);
  }
}

class CommandOperationalJournalPort implements OperationalJournalPort {
  private readonly config: CommandJournalConfig;

  public constructor(config: CommandJournalConfig) {
    this.config = config;
  }

  public async record(event: OperationalEvent): Promise<void> {
    await invokeJsonCommand<OperationalEvent, void>(this.config.record, event);
  }
}

function createCanBusPort(config: ClusterEmsCanBusConfig): CanBusPort {
  switch (config.kind) {
    case "state-file":
      return new JsonFileCanBusPort(config);
    case "overlay-file":
      return new OverlayBmsAdapter(new OverlayFileAssetPort(config));
    case "command":
      return new CommandCanBusPort(config);
  }
}

function createGridInverterPort(config: ClusterEmsGridInverterConfig): GridInverterPort {
  switch (config.kind) {
    case "state-file":
      return new StateFileGridInverterPort(config);
    case "modbus-tcp":
      return new ModbusTcpGridInverterPort(config);
    case "command":
      return new CommandGridInverterPort(config);
  }
}

function createHmiPort(config: ClusterEmsHmiConfig): HmiPort {
  switch (config.kind) {
    case "console":
      return new ConsoleHmiPort();
    case "file":
      return new FileHmiPort(config);
  }
}

function createWatchdogPort(config: ClusterEmsWatchdogConfig): WatchdogPort {
  switch (config.kind) {
    case "file":
      return new FileWatchdogPort(config);
    case "command":
      return new CommandWatchdogPort(config);
  }
}

function createJournalPort(config: ClusterEmsJournalConfig): OperationalJournalPort {
  switch (config.kind) {
    case "jsonl-file":
      return new JsonLineOperationalJournalPort(config);
    case "command":
      return new CommandOperationalJournalPort(config);
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

export class ClusterEmsDaemon {
  private readonly config: ClusterEmsDaemonConfig;
  private readonly controller: ClusterEmsController;
  private readonly canBus: CanBusPort;
  private readonly state: ClusterEmsDaemonState = {
    running: false
  };
  private readonly clock: ClockPort = new SystemClock();
  private serverReturn = createServer();
  private cycleTimer?: NodeJS.Timeout;
  private cycleInFlight = false;

  public constructor(config: ClusterEmsDaemonConfig) {
    this.config = config;
    this.canBus = createCanBusPort(config.canBus);
    this.controller = new ClusterEmsController({
      config: config.config,
      canBus: this.canBus,
      inverter: createGridInverterPort(config.inverter),
      hmi: createHmiPort(config.hmi),
      watchdog: createWatchdogPort(config.watchdog),
      clock: this.clock,
      journal: createJournalPort(config.journal)
    });
    this.serverReturn = createServer(async (request, response) => {
      await this.handleRequest(request, response);
    });
  }

  public snapshotState(): ClusterEmsDaemonState {
    return { ...this.state };
  }

  public async start(options: ClusterEmsDaemonStartOptions = {}): Promise<void> {
    if (this.state.running) {
      return;
    }

    this.state.running = true;
    this.state.startedAt = this.clock.now().toISOString();
    await new Promise<void>((resolvePromise, rejectPromise) => {
      this.serverReturn.once("error", rejectPromise);
      this.serverReturn.listen(this.config.http.port, this.config.http.host, () => {
        this.serverReturn.off("error", rejectPromise);
        resolvePromise();
      });
    });
    const address = this.serverReturn.address();
    const resolvedPort =
      typeof address === "object" && address !== null
        ? address.port
        : this.config.http.port;
    this.state.httpAddress = `http://${this.config.http.host}:${String(resolvedPort)}`;

    if (this.config.cycle.runOnStart ?? true) {
      await this.runCycle();
    }

    if (options.once) {
      await this.stop();
      return;
    }

    this.scheduleNextCycle();
  }

  public async stop(): Promise<void> {
    this.state.running = false;
    if (this.cycleTimer) {
      clearTimeout(this.cycleTimer);
      this.cycleTimer = undefined;
    }
    await new Promise<void>((resolvePromise) => {
      this.serverReturn.close(() => {
        resolvePromise();
      });
    });
  }

  public async runCycle(): Promise<ClusterTelemetry> {
    this.state.lastCycleAt = this.clock.now().toISOString();
    this.cycleInFlight = true;
    try {
      const snapshot = await this.controller.runCycle();
      this.state.lastSuccessAt = this.clock.now().toISOString();
      this.state.lastError = undefined;
      return snapshot;
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

    this.cycleTimer = setTimeout(async () => {
      if (!this.cycleInFlight) {
        try {
          await this.runCycle();
        } catch (error) {
          console.error("[EMS] cycle failed", error);
        }
      }
      this.scheduleNextCycle();
    }, this.config.cycle.intervalMs);
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

      if (method === "GET" && url.pathname === "/snapshot") {
        writeJsonResponse(response, 200, await this.controller.getSnapshot());
        return;
      }

      if (method === "GET" && url.pathname === "/alerts") {
        const shouldDrain = url.searchParams.get("drain") === "true";
        writeJsonResponse(
          response,
          200,
          shouldDrain
            ? await this.controller.drainAlerts()
            : await this.controller.drainAlerts().then((alerts) => alerts)
        );
        return;
      }

      if (method === "GET" && url.pathname === "/diagnostics") {
        writeJsonResponse(response, 200, await this.canBus.readDiagnostics());
        return;
      }

      if (method === "POST" && url.pathname === "/run-cycle") {
        writeJsonResponse(response, 200, await this.runCycle());
        return;
      }

      if (method === "POST" && url.pathname === "/commands") {
        const body = await readRequestBody(request);
        writeJsonResponse(
          response,
          200,
          await this.controller.applyRemoteCommand(body as RemoteCommand)
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

export function normalizeClusterEmsDaemonConfig(
  configDirectory: string,
  config: ClusterEmsDaemonConfig
): ClusterEmsDaemonConfig {
  const canBus = (() => {
    switch (config.canBus.kind) {
      case "state-file":
        return {
          ...config.canBus,
          statusesPath: resolvePathMaybeRelative(configDirectory, config.canBus.statusesPath) ?? config.canBus.statusesPath,
          diagnosticsPath: resolvePathMaybeRelative(configDirectory, config.canBus.diagnosticsPath),
          commandsPath: resolvePathMaybeRelative(configDirectory, config.canBus.commandsPath) ?? config.canBus.commandsPath,
          commandHistoryPath: resolvePathMaybeRelative(configDirectory, config.canBus.commandHistoryPath),
          isolatesPath: resolvePathMaybeRelative(configDirectory, config.canBus.isolatesPath)
        } satisfies JsonFileCanBusConfig;
      case "overlay-file":
        return {
          ...config.canBus,
          assetsPath: resolvePathMaybeRelative(configDirectory, config.canBus.assetsPath) ?? config.canBus.assetsPath,
          dispatchPath: resolvePathMaybeRelative(configDirectory, config.canBus.dispatchPath) ?? config.canBus.dispatchPath,
          dispatchHistoryPath: resolvePathMaybeRelative(configDirectory, config.canBus.dispatchHistoryPath),
          isolatesPath: resolvePathMaybeRelative(configDirectory, config.canBus.isolatesPath)
        } satisfies OverlayFileCanBusConfig;
      case "command":
        return {
          ...config.canBus,
          readStatuses: resolveCommandConfig(configDirectory, config.canBus.readStatuses),
          readDiagnostics: config.canBus.readDiagnostics
            ? resolveCommandConfig(configDirectory, config.canBus.readDiagnostics)
            : undefined,
          writeCommands: resolveCommandConfig(configDirectory, config.canBus.writeCommands),
          isolateNode: resolveCommandConfig(configDirectory, config.canBus.isolateNode)
        } satisfies CommandCanBusConfig;
    }
  })();

  const inverter = (() => {
    switch (config.inverter.kind) {
      case "state-file":
        return {
          ...config.inverter,
          statePath: resolvePathMaybeRelative(configDirectory, config.inverter.statePath) ?? config.inverter.statePath,
          setpointPath: resolvePathMaybeRelative(configDirectory, config.inverter.setpointPath) ?? config.inverter.setpointPath,
          setpointHistoryPath: resolvePathMaybeRelative(configDirectory, config.inverter.setpointHistoryPath),
          prechargePath: resolvePathMaybeRelative(configDirectory, config.inverter.prechargePath),
          holdOpenPath: resolvePathMaybeRelative(configDirectory, config.inverter.holdOpenPath)
        } satisfies StateFileGridInverterConfig;
      case "modbus-tcp":
        return config.inverter;
      case "command":
        return {
          ...config.inverter,
          readState: resolveCommandConfig(configDirectory, config.inverter.readState),
          writeSetpoint: resolveCommandConfig(configDirectory, config.inverter.writeSetpoint),
          prechargeDcBus: resolveCommandConfig(configDirectory, config.inverter.prechargeDcBus),
          holdOpenBus: resolveCommandConfig(configDirectory, config.inverter.holdOpenBus)
        } satisfies CommandGridInverterConfig;
    }
  })();

  const hmi = config.hmi.kind === "file"
    ? {
        ...config.hmi,
        snapshotPath: resolvePathMaybeRelative(configDirectory, config.hmi.snapshotPath) ?? config.hmi.snapshotPath,
        alertsPath: resolvePathMaybeRelative(configDirectory, config.hmi.alertsPath)
      } satisfies FileHmiConfig
    : config.hmi;

  const watchdog = config.watchdog.kind === "file"
    ? {
        ...config.watchdog,
        heartbeatPath: resolvePathMaybeRelative(configDirectory, config.watchdog.heartbeatPath) ?? config.watchdog.heartbeatPath,
        failSafePath: resolvePathMaybeRelative(configDirectory, config.watchdog.failSafePath)
      } satisfies FileWatchdogConfig
    : {
        ...config.watchdog,
        kick: resolveCommandConfig(configDirectory, config.watchdog.kick),
        triggerFailSafe: resolveCommandConfig(configDirectory, config.watchdog.triggerFailSafe)
      } satisfies CommandWatchdogConfig;

  const journal = config.journal.kind === "jsonl-file"
    ? {
        ...config.journal,
        path: resolvePathMaybeRelative(configDirectory, config.journal.path) ?? config.journal.path
      } satisfies JsonLineJournalConfig
    : {
        ...config.journal,
        record: resolveCommandConfig(configDirectory, config.journal.record)
      } satisfies CommandJournalConfig;

  return {
    ...config,
    canBus,
    inverter,
    hmi,
    watchdog,
    journal
  };
}

export async function loadClusterEmsDaemonConfig(
  configPath: string
): Promise<ClusterEmsDaemonConfig> {
  const config = await readJsonFile<ClusterEmsDaemonConfig | null>(configPath, null);
  if (!config) {
    throw new Error(`EMS config file not found: ${configPath}`);
  }

  return normalizeClusterEmsDaemonConfig(
    dirname(configPath),
    resolveEnvPlaceholders(config)
  );
}

export interface ClusterEmsCliOptions {
  configPath: string;
  once: boolean;
}

export function parseClusterEmsCliOptions(argv: string[]): ClusterEmsCliOptions {
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
