import net from "node:net";

function parseFrames(buffer) {
  const frames = [];
  let offset = 0;
  while (buffer.length - offset >= 7) {
    const length = buffer.readUInt16BE(offset + 4);
    const frameLength = 6 + length;
    if (buffer.length - offset < frameLength) {
      break;
    }
    frames.push(buffer.subarray(offset, offset + frameLength));
    offset += frameLength;
  }
  return {
    frames,
    remaining: buffer.subarray(offset)
  };
}

export class FakeModbusServer {
  constructor(initialRegisters = {}) {
    this.holdingRegisters = new Map(
      Object.entries(initialRegisters).map(([address, value]) => [
        Number(address),
        Number(value)
      ])
    );
    this.writeLog = [];
    this.server = net.createServer((socket) => {
      let pending = Buffer.alloc(0);
      socket.on("data", (chunk) => {
        pending = Buffer.concat([pending, chunk]);
        const parsed = parseFrames(pending);
        pending = parsed.remaining;
        for (const frame of parsed.frames) {
          this.handleFrame(socket, frame);
        }
      });
    });
  }

  async start() {
    await new Promise((resolve, reject) => {
      this.server.once("error", reject);
      this.server.listen(0, "127.0.0.1", () => {
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

  setHoldingRegister(address, value) {
    this.holdingRegisters.set(address, value);
  }

  getWrites() {
    return [...this.writeLog];
  }

  handleFrame(socket, frame) {
    const transactionId = frame.readUInt16BE(0);
    const unitId = frame.readUInt8(6);
    const functionCode = frame.readUInt8(7);
    if (functionCode === 0x03) {
      const startAddress = frame.readUInt16BE(8);
      const count = frame.readUInt16BE(10);
      const payload = Buffer.alloc(2 + count * 2);
      payload.writeUInt8(0x03, 0);
      payload.writeUInt8(count * 2, 1);
      for (let index = 0; index < count; index += 1) {
        payload.writeUInt16BE(
          this.holdingRegisters.get(startAddress + index) ?? 0,
          2 + index * 2
        );
      }
      socket.write(this.wrapResponse(transactionId, unitId, payload));
      return;
    }

    if (functionCode === 0x06) {
      const address = frame.readUInt16BE(8);
      const value = frame.readUInt16BE(10);
      this.holdingRegisters.set(address, value);
      this.writeLog.push({ address, value });
      socket.write(this.wrapResponse(transactionId, unitId, frame.subarray(7, 12)));
      return;
    }

    const exception = Buffer.from([functionCode | 0x80, 0x01]);
    socket.write(this.wrapResponse(transactionId, unitId, exception));
  }

  wrapResponse(transactionId, unitId, payload) {
    const frame = Buffer.alloc(7 + payload.length);
    frame.writeUInt16BE(transactionId, 0);
    frame.writeUInt16BE(0, 2);
    frame.writeUInt16BE(payload.length + 1, 4);
    frame.writeUInt8(unitId, 6);
    payload.copy(frame, 7);
    return frame;
  }
}
