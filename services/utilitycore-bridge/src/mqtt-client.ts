import { Socket } from "node:net";
import { connect as connectTls, type ConnectionOptions, type TLSSocket } from "node:tls";
import { readFile } from "node:fs/promises";
import type { MqttBrokerPort } from "./bridge-service.ts";

export interface MqttTcpClientConfig {
  kind: "mqtt-tcp";
  host: string;
  port: number;
  clientId: string;
  username?: string;
  password?: string;
  keepAliveSeconds?: number;
  tls?: {
    enabled: boolean;
    servername?: string;
    rejectUnauthorized?: boolean;
    caCertPath?: string;
  };
}

interface PendingAck {
  resolve(): void;
  reject(error: Error): void;
}

function encodeUtf8String(value: string): Buffer {
  const payload = Buffer.from(value, "utf8");
  const prefix = Buffer.alloc(2);
  prefix.writeUInt16BE(payload.byteLength, 0);
  return Buffer.concat([prefix, payload]);
}

function encodeRemainingLength(value: number): Buffer {
  const bytes: number[] = [];
  let remaining = value;
  do {
    let encoded = remaining % 128;
    remaining = Math.floor(remaining / 128);
    if (remaining > 0) {
      encoded |= 0x80;
    }
    bytes.push(encoded);
  } while (remaining > 0);
  return Buffer.from(bytes);
}

function tryParsePacket(buffer: Buffer): { packet?: Buffer; consumed: number } {
  if (buffer.byteLength < 2) {
    return { consumed: 0 };
  }

  let multiplier = 1;
  let remainingLength = 0;
  let index = 1;
  while (index < buffer.byteLength) {
    const encoded = buffer[index] ?? 0;
    remainingLength += (encoded & 0x7f) * multiplier;
    multiplier *= 128;
    index += 1;
    if ((encoded & 0x80) === 0) {
      const packetLength = index + remainingLength;
      if (buffer.byteLength < packetLength) {
        return { consumed: 0 };
      }
      return {
        packet: buffer.subarray(0, packetLength),
        consumed: packetLength
      };
    }
  }

  return { consumed: 0 };
}

export class MqttTcpClient implements MqttBrokerPort {
  private readonly config: MqttTcpClientConfig;
  private socket?: Socket | TLSSocket;
  private connectPromise?: Promise<void>;
  private connectResolver?: PendingAck;
  private incomingBuffer = Buffer.alloc(0);
  private readonly subscriptions = new Map<
    string,
    (payload: string) => Promise<void>
  >();
  private readonly pendingSubAcks = new Map<number, PendingAck>();
  private packetIdentifier = 1;
  private pingTimer?: NodeJS.Timeout;
  private connected = false;

  public constructor(config: MqttTcpClientConfig) {
    this.config = config;
  }

  public async publish(topic: string, payload: string): Promise<void> {
    await this.ensureConnected();
    const topicBytes = encodeUtf8String(topic);
    const payloadBytes = Buffer.from(payload, "utf8");
    const packet = Buffer.concat([
      Buffer.from([0x30]),
      encodeRemainingLength(topicBytes.byteLength + payloadBytes.byteLength),
      topicBytes,
      payloadBytes
    ]);
    this.socket?.write(packet);
  }

  public async subscribe(
    topic: string,
    handler: (payload: string) => Promise<void>
  ): Promise<void> {
    this.subscriptions.set(topic, handler);
    await this.ensureConnected();
    await this.sendSubscribe(topic);
  }

  public async close(): Promise<void> {
    this.connected = false;
    if (this.pingTimer) {
      clearInterval(this.pingTimer);
      this.pingTimer = undefined;
    }

    const socket = this.socket;
    this.socket = undefined;
    if (!socket) {
      return;
    }

    await new Promise<void>((resolvePromise) => {
      socket.once("close", () => {
        resolvePromise();
      });
      socket.end();
      socket.destroy();
    });
  }

  private async ensureConnected(): Promise<void> {
    if (this.connected) {
      return;
    }

    if (this.connectPromise) {
      return this.connectPromise;
    }

    this.connectPromise = this.openConnection();
    try {
      await this.connectPromise;
    } finally {
      this.connectPromise = undefined;
    }
  }

  private async openConnection(): Promise<void> {
    const socket = await this.createSocket();
    this.socket = socket;
    this.connected = false;

    socket.setKeepAlive(true);
    socket.on("data", (chunk) => {
      this.onData(chunk);
    });
    socket.on("close", () => {
      this.connected = false;
      if (this.pingTimer) {
        clearInterval(this.pingTimer);
        this.pingTimer = undefined;
      }
      if (this.connectResolver) {
        this.connectResolver.reject(new Error("MQTT connection closed before CONNACK."));
        this.connectResolver = undefined;
      }
    });
    socket.on("error", (error) => {
      if (this.connectResolver) {
        this.connectResolver.reject(error instanceof Error ? error : new Error(String(error)));
        this.connectResolver = undefined;
      }
    });

    await new Promise<void>((resolvePromise, rejectPromise) => {
      this.connectResolver = {
        resolve: () => {
          resolvePromise();
        },
        reject: rejectPromise
      };

      const protocol = encodeUtf8String("MQTT");
      const keepAlive = this.config.keepAliveSeconds ?? 30;
      let flags = 0b0000_0010;
      const payloadParts = [encodeUtf8String(this.config.clientId)];
      if (this.config.username) {
        flags |= 0b1000_0000;
        payloadParts.push(encodeUtf8String(this.config.username));
      }
      if (this.config.password) {
        flags |= 0b0100_0000;
        payloadParts.push(encodeUtf8String(this.config.password));
      }
      const variableHeader = Buffer.concat([
        protocol,
        Buffer.from([0x04, flags]),
        Buffer.from([(keepAlive >> 8) & 0xff, keepAlive & 0xff])
      ]);
      const payload = Buffer.concat(payloadParts);
      const packet = Buffer.concat([
        Buffer.from([0x10]),
        encodeRemainingLength(variableHeader.byteLength + payload.byteLength),
        variableHeader,
        payload
      ]);
      socket.write(packet);
    });

    for (const topic of this.subscriptions.keys()) {
      await this.sendSubscribe(topic);
    }

    const keepAliveMs = Math.max(5, this.config.keepAliveSeconds ?? 30) * 500;
    this.pingTimer = setInterval(() => {
      if (!this.connected || !this.socket) {
        return;
      }
      this.socket.write(Buffer.from([0xc0, 0x00]));
    }, keepAliveMs);
  }

  private async createSocket(): Promise<Socket | TLSSocket> {
    if (!this.config.tls?.enabled) {
      return new Promise<Socket>((resolvePromise, rejectPromise) => {
        const socket = new Socket();
        socket.once("error", rejectPromise);
        socket.connect(this.config.port, this.config.host, () => {
          socket.off("error", rejectPromise);
          resolvePromise(socket);
        });
      });
    }

    const tlsOptions: ConnectionOptions = {
      host: this.config.host,
      port: this.config.port,
      servername: this.config.tls.servername ?? this.config.host,
      rejectUnauthorized: this.config.tls.rejectUnauthorized ?? true
    };
    if (this.config.tls.caCertPath) {
      tlsOptions.ca = await readFile(this.config.tls.caCertPath);
    }

    return new Promise<TLSSocket>((resolvePromise, rejectPromise) => {
      const socket = connectTls(tlsOptions, () => {
        resolvePromise(socket);
      });
      socket.once("error", rejectPromise);
    });
  }

  private async sendSubscribe(topic: string): Promise<void> {
    const packetId = this.nextPacketIdentifier();
    const topicBytes = encodeUtf8String(topic);
    const payload = Buffer.concat([topicBytes, Buffer.from([0x00])]);
    const variableHeader = Buffer.alloc(2);
    variableHeader.writeUInt16BE(packetId, 0);
    const packet = Buffer.concat([
      Buffer.from([0x82]),
      encodeRemainingLength(variableHeader.byteLength + payload.byteLength),
      variableHeader,
      payload
    ]);

    await new Promise<void>((resolvePromise, rejectPromise) => {
      this.pendingSubAcks.set(packetId, {
        resolve: resolvePromise,
        reject: rejectPromise
      });
      this.socket?.write(packet);
    });
  }

  private onData(chunk: Buffer): void {
    this.incomingBuffer = Buffer.concat([this.incomingBuffer, chunk]);
    while (true) {
      const parsed = tryParsePacket(this.incomingBuffer);
      if (!parsed.packet) {
        break;
      }
      this.incomingBuffer = this.incomingBuffer.subarray(parsed.consumed);
      void this.handlePacket(parsed.packet);
    }
  }

  private async handlePacket(packet: Buffer): Promise<void> {
    const packetType = packet.readUInt8(0) >> 4;
    const headerSize = packet.byteLength - this.extractRemainingLength(packet).remainingLength;
    const payload = packet.subarray(headerSize);

    if (packetType === 2) {
      const returnCode = payload.readUInt8(1);
      if (returnCode !== 0) {
        this.connectResolver?.reject(new Error(`MQTT CONNACK returned ${String(returnCode)}`));
        this.connectResolver = undefined;
        return;
      }
      this.connected = true;
      this.connectResolver?.resolve();
      this.connectResolver = undefined;
      return;
    }

    if (packetType === 9) {
      const packetId = payload.readUInt16BE(0);
      const pending = this.pendingSubAcks.get(packetId);
      if (pending) {
        this.pendingSubAcks.delete(packetId);
        pending.resolve();
      }
      return;
    }

    if (packetType === 13) {
      return;
    }

    if (packetType !== 3) {
      return;
    }

    const topicLength = payload.readUInt16BE(0);
    const topic = payload.subarray(2, 2 + topicLength).toString("utf8");
    const packetFlags = packet.readUInt8(0) & 0x0f;
    const qos = (packetFlags & 0b0110) >> 1;
    let cursor = 2 + topicLength;
    let packetId: number | undefined;
    if (qos > 0) {
      packetId = payload.readUInt16BE(cursor);
      cursor += 2;
    }
    const message = payload.subarray(cursor).toString("utf8");
    const handler = this.subscriptions.get(topic);
    if (handler) {
      await handler(message);
    }

    if (qos === 1 && packetId !== undefined) {
      const ack = Buffer.from([0x40, 0x02, (packetId >> 8) & 0xff, packetId & 0xff]);
      this.socket?.write(ack);
    }
  }

  private extractRemainingLength(packet: Buffer): { remainingLength: number; bytesUsed: number } {
    let multiplier = 1;
    let remainingLength = 0;
    let index = 1;
    while (index < packet.byteLength) {
      const encoded = packet[index] ?? 0;
      remainingLength += (encoded & 0x7f) * multiplier;
      multiplier *= 128;
      index += 1;
      if ((encoded & 0x80) === 0) {
        return {
          remainingLength,
          bytesUsed: index - 1
        };
      }
    }
    return {
      remainingLength: 0,
      bytesUsed: 0
    };
  }

  private nextPacketIdentifier(): number {
    this.packetIdentifier = (this.packetIdentifier % 0xffff) + 1;
    return this.packetIdentifier;
  }
}
