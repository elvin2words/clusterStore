#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import sys
import time
from typing import Any


HEARTBEAT_BASE_ID = 0x650
COMMAND_BASE_ID = 0x750
ACK_BASE_ID = 0x760


def load_can_module() -> Any:
    try:
        import can  # type: ignore
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "python-can is required for this bench script. Install it with `pip install python-can`."
        ) from exc
    return can


def open_bus(args: argparse.Namespace) -> Any:
    can = load_can_module()
    return can.Bus(interface=args.interface, channel=args.channel, bitrate=args.bitrate)


def heartbeat_id(node_id: int) -> int:
    return HEARTBEAT_BASE_ID + node_id


def command_id(node_id: int) -> int:
    return COMMAND_BASE_ID + node_id


def ack_id(node_id: int) -> int:
    return ACK_BASE_ID + node_id


def decode_heartbeat(data: bytes) -> str:
    if len(data) < 8:
        return f"short heartbeat payload: {data.hex()}"

    pack_dv = struct.unpack_from("<H", data, 0)[0]
    bus_dv = struct.unpack_from("<H", data, 2)[0]
    current_da = struct.unpack_from("<h", data, 4)[0]
    temperature_c = struct.unpack_from("<b", data, 6)[0]
    flags = data[7]
    return (
        f"pack={pack_dv / 10.0:.1f}V "
        f"bus={bus_dv / 10.0:.1f}V "
        f"current={current_da / 10.0:.1f}A "
        f"temp={temperature_c}C "
        f"flags=0x{flags:02x}"
    )


def decode_ack(data: bytes) -> str:
    if len(data) < 8:
        return f"short ack payload: {data.hex()}"

    opcode = data[0]
    sequence = data[1]
    uptime_s = struct.unpack_from("<I", data, 2)[0]
    signature = bytes(data[6:8]).decode("ascii", errors="replace")
    return (
        f"opcode=0x{opcode:02x} seq={sequence} uptime_s={uptime_s} signature={signature}"
    )


def send_command(bus: Any, node_id: int, opcode: int, sequence: int) -> None:
    can = load_can_module()
    message = can.Message(
        arbitration_id=command_id(node_id),
        is_extended_id=False,
        data=bytes([opcode & 0xFF, sequence & 0xFF, 0, 0, 0, 0, 0, 0]),
    )
    bus.send(message)


def monitor(args: argparse.Namespace) -> int:
    bus = open_bus(args)
    deadline = None if args.duration <= 0 else (time.monotonic() + args.duration)
    hb_id = heartbeat_id(args.node_id)
    ack_frame_id = ack_id(args.node_id)

    print(
        f"Monitoring node {args.node_id} on {args.interface}:{args.channel} at {args.bitrate} bps"
    )
    try:
        while deadline is None or time.monotonic() < deadline:
            message = bus.recv(timeout=0.5)
            if message is None:
                continue

            if message.arbitration_id == hb_id:
                print(f"HEARTBEAT {decode_heartbeat(bytes(message.data))}")
            elif message.arbitration_id == ack_frame_id:
                print(f"ACK {decode_ack(bytes(message.data))}")
            else:
                print(
                    f"FRAME id=0x{message.arbitration_id:03x} dlc={message.dlc} data={bytes(message.data).hex()}"
                )
    finally:
        bus.shutdown()

    return 0


def await_ack(bus: Any, node_id: int, timeout_s: float) -> int:
    target_id = ack_id(node_id)
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        message = bus.recv(timeout=0.2)
        if message is None:
            continue
        if message.arbitration_id == target_id:
            print(f"ACK {decode_ack(bytes(message.data))}")
            return 0
        if message.arbitration_id == heartbeat_id(node_id):
            print(f"HEARTBEAT {decode_heartbeat(bytes(message.data))}")
    print("Timed out waiting for ACK.", file=sys.stderr)
    return 1


def ping(args: argparse.Namespace) -> int:
    bus = open_bus(args)
    try:
        send_command(bus, args.node_id, 0x01, args.sequence)
        return await_ack(bus, args.node_id, args.timeout)
    finally:
        bus.shutdown()


def sample(args: argparse.Namespace) -> int:
    bus = open_bus(args)
    heartbeat_seen = False
    deadline = time.monotonic() + args.timeout
    try:
        send_command(bus, args.node_id, 0x02, args.sequence)
        while time.monotonic() < deadline:
            message = bus.recv(timeout=0.2)
            if message is None:
                continue

            if message.arbitration_id == heartbeat_id(args.node_id):
                heartbeat_seen = True
                print(f"HEARTBEAT {decode_heartbeat(bytes(message.data))}")
            elif message.arbitration_id == ack_id(args.node_id):
                print(f"ACK {decode_ack(bytes(message.data))}")
                return 0 if heartbeat_seen else 2
        print("Timed out waiting for sample ACK.", file=sys.stderr)
        return 1
    finally:
        bus.shutdown()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="ClusterStore STM32G474 CAN-only bench harness")
    parser.add_argument("--interface", default="socketcan", help="python-can interface name")
    parser.add_argument("--channel", default="can0", help="CAN channel name")
    parser.add_argument("--bitrate", default=500000, type=int, help="nominal CAN bitrate")
    parser.add_argument("--node-id", default=1, type=int, help="bench node id")

    subparsers = parser.add_subparsers(dest="command", required=True)

    monitor_parser = subparsers.add_parser("monitor", help="print heartbeat and ack frames")
    monitor_parser.add_argument(
        "--duration",
        default=0.0,
        type=float,
        help="seconds to monitor, 0 means run until interrupted",
    )
    monitor_parser.set_defaults(func=monitor)

    ping_parser = subparsers.add_parser("ping", help="send a ping command and wait for ack")
    ping_parser.add_argument("--sequence", default=1, type=int, help="command sequence byte")
    ping_parser.add_argument("--timeout", default=3.0, type=float, help="ack timeout seconds")
    ping_parser.set_defaults(func=ping)

    sample_parser = subparsers.add_parser(
        "sample", help="request an immediate sample heartbeat and ack"
    )
    sample_parser.add_argument("--sequence", default=1, type=int, help="command sequence byte")
    sample_parser.add_argument("--timeout", default=3.0, type=float, help="ack timeout seconds")
    sample_parser.set_defaults(func=sample)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
