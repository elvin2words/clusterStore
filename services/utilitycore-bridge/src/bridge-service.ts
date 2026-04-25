import {
  alertsTopic,
  commandAckTopic,
  commandsTopic,
  telemetryTopic,
  wrapAlert,
  wrapCommandAck,
  wrapTelemetry
} from "@clusterstore/contracts";
import type {
  ClusterAlert,
  ClusterTelemetry,
  CommandAcknowledgement,
  MqttEnvelope,
  OperationalEvent,
  RemoteCommand
} from "@clusterstore/contracts";
import { validateRemoteCommand } from "./command-router.ts";

export interface ClockPort {
  now(): Date;
}

export interface MqttBrokerPort {
  publish(topic: string, payload: string): Promise<void>;
  subscribe(
    topic: string,
    handler: (payload: string) => Promise<void>
  ): Promise<void>;
}

export interface LteModemPort {
  isOnline(): Promise<boolean>;
  signalQualityRssi(): Promise<number>;
}

export interface LocalScadaPort {
  publishTelemetry(snapshot: ClusterTelemetry): Promise<void>;
  publishAlerts(alerts: ClusterAlert[]): Promise<void>;
}

export interface BufferedMessage {
  id: string;
  topic: string;
  payload: string;
  createdAt: string;
}

export interface TelemetryBufferPort {
  enqueue(message: BufferedMessage): Promise<void>;
  peekPending(limit: number): Promise<BufferedMessage[]>;
  acknowledge(messageIds: string[]): Promise<void>;
}

export interface CommandAuthorizerPort {
  authorize(command: RemoteCommand): Promise<{
    authorized: boolean;
    reason?: string;
  }>;
}

export interface CommandLedgerPort {
  hasSeen(idempotencyKey: string): Promise<boolean>;
  recordReceived(command: RemoteCommand): Promise<void>;
  recordAcknowledgement(acknowledgement: CommandAcknowledgement): Promise<void>;
}

export interface OperationalJournalPort {
  record(event: OperationalEvent): Promise<void>;
}

export interface ClusterEmsApi {
  getSnapshot(): Promise<ClusterTelemetry>;
  drainAlerts(): Promise<ClusterAlert[]>;
  applyRemoteCommand(command: RemoteCommand): Promise<CommandAcknowledgement>;
}

export interface UtilityCoreBridgeConfig {
  siteId: string;
  clusterId: string;
  maxCommandTtlMs: number;
  replayBatchSize: number;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null;
}

function unwrapCommandPayload(payload: string): RemoteCommand {
  const parsed = JSON.parse(payload) as unknown;
  const envelopePayload =
    isRecord(parsed) &&
    "payload" in parsed &&
    "schemaVersion" in parsed &&
    "sentAt" in parsed
      ? (parsed as MqttEnvelope<unknown>).payload
      : parsed;

  if (!isRecord(envelopePayload)) {
    throw new Error("Received command payload is not an object.");
  }

  return envelopePayload as RemoteCommand;
}

export class UtilityCoreBridgeService {
  private readonly config: UtilityCoreBridgeConfig;
  private readonly clock: ClockPort;
  private readonly mqtt: MqttBrokerPort;
  private readonly lte: LteModemPort;
  private readonly buffer: TelemetryBufferPort;
  private readonly scada: LocalScadaPort;
  private readonly authorizer: CommandAuthorizerPort;
  private readonly commandLedger: CommandLedgerPort;
  private readonly journal: OperationalJournalPort;
  private readonly ems: ClusterEmsApi;

  public constructor(
    config: UtilityCoreBridgeConfig,
    clock: ClockPort,
    mqtt: MqttBrokerPort,
    lte: LteModemPort,
    buffer: TelemetryBufferPort,
    scada: LocalScadaPort,
    authorizer: CommandAuthorizerPort,
    commandLedger: CommandLedgerPort,
    journal: OperationalJournalPort,
    ems: ClusterEmsApi
  ) {
    this.config = config;
    this.clock = clock;
    this.mqtt = mqtt;
    this.lte = lte;
    this.buffer = buffer;
    this.scada = scada;
    this.authorizer = authorizer;
    this.commandLedger = commandLedger;
    this.journal = journal;
    this.ems = ems;
  }

  public async bindCommandSubscription(): Promise<void> {
    await this.mqtt.subscribe(
      commandsTopic(this.config.siteId, this.config.clusterId),
      async (payload) => {
        let command: RemoteCommand;
        try {
          command = unwrapCommandPayload(payload);
        } catch (error) {
          await this.journal.record({
            siteId: this.config.siteId,
            clusterId: this.config.clusterId,
            timestamp: this.clock.now().toISOString(),
            kind: "command.invalid_payload",
            severity: "warning",
            message:
              error instanceof Error
                ? error.message
                : "Received a malformed command payload."
          });
          return;
        }

        const issues = validateRemoteCommand(command, {
          siteId: this.config.siteId,
          clusterId: this.config.clusterId,
          now: this.clock.now(),
          maxCommandTtlMs: this.config.maxCommandTtlMs
        });

        if (issues.length > 0) {
          await this.publishCommandAck({
            commandId: command.id,
            idempotencyKey: command.idempotencyKey,
            status: "rejected",
            timestamp: this.clock.now().toISOString(),
            reason: issues.join(" ")
          });
          return;
        }

        if (await this.commandLedger.hasSeen(command.idempotencyKey)) {
          await this.publishCommandAck({
            commandId: command.id,
            idempotencyKey: command.idempotencyKey,
            status: "duplicate",
            timestamp: this.clock.now().toISOString(),
            reason: "Command idempotency key has already been processed."
          });
          return;
        }

        await this.commandLedger.recordReceived(command);

        const authorization = await this.authorizer.authorize(command);
        if (!authorization.authorized) {
          await this.publishCommandAck({
            commandId: command.id,
            idempotencyKey: command.idempotencyKey,
            status: "rejected",
            timestamp: this.clock.now().toISOString(),
            reason: authorization.reason ?? "Command authorization failed."
          });
          return;
        }

        await this.publishCommandAck({
          commandId: command.id,
          idempotencyKey: command.idempotencyKey,
          status: "accepted",
          timestamp: this.clock.now().toISOString()
        });

        try {
          const finalAck = await this.ems.applyRemoteCommand(command);
          await this.publishCommandAck(finalAck);
        } catch (error) {
          await this.publishCommandAck({
            commandId: command.id,
            idempotencyKey: command.idempotencyKey,
            status: "rejected",
            timestamp: this.clock.now().toISOString(),
            reason:
              error instanceof Error
                ? error.message
                : "EMS command application failed."
          });
        }
      }
    );
  }

  public async publishCycle(): Promise<void> {
    const snapshot = await this.ems.getSnapshot();
    const alerts = await this.ems.drainAlerts();
    const timestamp = this.clock.now().toISOString();
    const telemetryMessage: BufferedMessage = {
      id: `telemetry:${snapshot.timestamp}`,
      topic: telemetryTopic(this.config.siteId, this.config.clusterId),
      payload: JSON.stringify(wrapTelemetry(snapshot.timestamp, snapshot)),
      createdAt: timestamp
    };
    const alertMessages = alerts.map<BufferedMessage>((alert) => ({
      id: `alert:${alert.id}`,
      topic: alertsTopic(this.config.siteId, this.config.clusterId),
      payload: JSON.stringify(wrapAlert(alert.timestamp, alert)),
      createdAt: timestamp
    }));

    await this.scada.publishTelemetry(snapshot);
    await this.scada.publishAlerts(alerts);

    if (!(await this.lte.isOnline())) {
      await this.enqueueMessages([telemetryMessage, ...alertMessages]);
      await this.journal.record({
        siteId: this.config.siteId,
        clusterId: this.config.clusterId,
        timestamp,
        kind: "bridge.buffering",
        severity: "warning",
        message: "LTE offline, buffering telemetry for replay."
      });
      return;
    }

    const replayedPending = await this.replayBufferedMessages(timestamp);
    if (!replayedPending) {
      await this.enqueueMessages([telemetryMessage, ...alertMessages]);
      return;
    }

    await this.publishLiveMessages([telemetryMessage, ...alertMessages], timestamp);
  }

  private async publishCommandAck(
    acknowledgement: CommandAcknowledgement
  ): Promise<void> {
    if (acknowledgement.idempotencyKey) {
      await this.commandLedger.recordAcknowledgement(acknowledgement);
    }
    await this.mqtt.publish(
      commandAckTopic(this.config.siteId, this.config.clusterId),
      JSON.stringify(
        wrapCommandAck(acknowledgement.timestamp, acknowledgement)
      )
    );
    await this.journal.record({
      siteId: this.config.siteId,
      clusterId: this.config.clusterId,
      timestamp: acknowledgement.timestamp,
      kind: `command.${acknowledgement.status}`,
      severity: acknowledgement.status === "rejected" ? "warning" : "info",
      message: `Command ${acknowledgement.commandId} is ${acknowledgement.status}.`,
      metadata: {
        idempotencyKey: acknowledgement.idempotencyKey,
        reason: acknowledgement.reason,
        appliedClusterMode: acknowledgement.appliedClusterMode
      }
    });
  }

  private async enqueueMessages(messages: BufferedMessage[]): Promise<void> {
    for (const message of messages) {
      await this.buffer.enqueue(message);
    }
  }

  private async replayBufferedMessages(timestamp: string): Promise<boolean> {
    const pending = await this.buffer.peekPending(this.config.replayBatchSize);
    for (const bufferedMessage of pending) {
      try {
        await this.mqtt.publish(bufferedMessage.topic, bufferedMessage.payload);
        await this.buffer.acknowledge([bufferedMessage.id]);
      } catch {
        await this.journal.record({
          siteId: this.config.siteId,
          clusterId: this.config.clusterId,
          timestamp,
          kind: "bridge.replay_failed",
          severity: "warning",
          message: "MQTT replay failed, leaving buffered messages pending."
        });
        return false;
      }
    }

    return true;
  }

  private async publishLiveMessages(
    messages: BufferedMessage[],
    timestamp: string
  ): Promise<void> {
    for (let index = 0; index < messages.length; index += 1) {
      const message = messages[index];
      try {
        await this.mqtt.publish(message.topic, message.payload);
      } catch {
        await this.enqueueMessages(messages.slice(index));
        await this.journal.record({
          siteId: this.config.siteId,
          clusterId: this.config.clusterId,
          timestamp,
          kind: "bridge.publish_failed",
          severity: "warning",
          message: "MQTT publish failed, buffering unsent telemetry for retry."
        });
        return;
      }
    }
  }
}
