import net from "node:net";

function encodeRemainingLength(value) {
  const bytes = [];
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

function encodeUtf8String(value) {
  const payload = Buffer.from(value, "utf8");
  const prefix = Buffer.alloc(2);
  prefix.writeUInt16BE(payload.length, 0);
  return Buffer.concat([prefix, payload]);
}

function tryParsePacket(buffer) {
  if (buffer.length < 2) {
    return { consumed: 0 };
  }

  let multiplier = 1;
  let remainingLength = 0;
  let index = 1;
  while (index < buffer.length) {
    const encoded = buffer[index];
    remainingLength += (encoded & 0x7f) * multiplier;
    multiplier *= 128;
    index += 1;
    if ((encoded & 0x80) === 0) {
      const total = index + remainingLength;
      if (buffer.length < total) {
        return { consumed: 0 };
      }
      return {
        packet: buffer.subarray(0, total),
        consumed: total
      };
    }
  }

  return { consumed: 0 };
}

function packetPayload(packet) {
  let index = 1;
  while (index < packet.length) {
    const encoded = packet[index];
    index += 1;
    if ((encoded & 0x80) === 0) {
      return packet.subarray(index);
    }
  }
  return Buffer.alloc(0);
}

export class FakeMqttBroker {
  constructor() {
    this.messages = [];
    this.server = net.createServer((socket) => {
      const client = {
        socket,
        subscriptions: new Set()
      };
      this.clients.add(client);
      let pending = Buffer.alloc(0);
      socket.on("data", (chunk) => {
        pending = Buffer.concat([pending, chunk]);
        while (true) {
          const parsed = tryParsePacket(pending);
          if (!parsed.packet) {
            break;
          }
          pending = pending.subarray(parsed.consumed);
          this.handlePacket(client, parsed.packet);
        }
      });
      socket.on("close", () => {
        this.clients.delete(client);
      });
    });
    this.clients = new Set();
  }

  async start(options = {}) {
    const host = options.host ?? "127.0.0.1";
    const port = options.port ?? 0;
    await new Promise((resolve, reject) => {
      this.server.once("error", reject);
      this.server.listen(port, host, () => {
        this.server.off("error", reject);
        resolve();
      });
    });

    const address = this.server.address();
    this.host = address.address;
    this.port = address.port;
    return {
      host: this.host,
      port: this.port
    };
  }

  async stop() {
    await new Promise((resolve) => {
      this.server.close(() => resolve());
    });
  }

  async publish(topic, payload) {
    const packet = Buffer.concat([
      Buffer.from([0x30]),
      encodeRemainingLength(encodeUtf8String(topic).length + Buffer.byteLength(payload, "utf8")),
      encodeUtf8String(topic),
      Buffer.from(payload, "utf8")
    ]);
    for (const client of this.clients) {
      if (client.subscriptions.has(topic)) {
        client.socket.write(packet);
      }
    }
  }

  handlePacket(client, packet) {
    const packetType = packet.readUInt8(0) >> 4;
    const payload = packetPayload(packet);

    if (packetType === 1) {
      client.socket.write(Buffer.from([0x20, 0x02, 0x00, 0x00]));
      return;
    }

    if (packetType === 8) {
      const packetId = payload.readUInt16BE(0);
      const topicLength = payload.readUInt16BE(2);
      const topic = payload.subarray(4, 4 + topicLength).toString("utf8");
      client.subscriptions.add(topic);
      client.socket.write(
        Buffer.from([0x90, 0x03, (packetId >> 8) & 0xff, packetId & 0xff, 0x00])
      );
      return;
    }

    if (packetType === 3) {
      const topicLength = payload.readUInt16BE(0);
      const topic = payload.subarray(2, 2 + topicLength).toString("utf8");
      const message = payload.subarray(2 + topicLength).toString("utf8");
      this.messages.push({ topic, payload: message });
      return;
    }

    if (packetType === 12) {
      client.socket.write(Buffer.from([0xd0, 0x00]));
    }
  }
}
