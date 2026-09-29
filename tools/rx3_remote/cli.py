#!/usr/bin/env python3
"""Inspect controls, follow physical events, or send one raw RX3 control tuple."""

from __future__ import annotations

import argparse
import json
import random
import sys
import time

from .client import DEFAULT_HOST, DEFAULT_PORT, Connection, RemoteError, reconnecting_events
from .controls import CONTROLS, resolve_control
from .protocol import (
    OP_ABSOLUTE_MOVED, OP_PRESSED, OP_RELEASED, OPERATION_NAMES,
    ControlTuple, ProtocolError, SOURCE_PHYSICAL, SOURCE_REMOTE,
)


def parse_operation(value: str) -> int:
    names = {name: code for code, name in OPERATION_NAMES.items()}
    try:
        return names[value.lower()]
    except KeyError:
        try:
            return int(value, 0)
        except ValueError as error:
            choices = ", ".join(names)
            raise argparse.ArgumentTypeError(
                f"operation must be a number or one of: {choices}"
            ) from error


def event_dict(event) -> dict[str, object]:
    control = event.control
    return {
        "type": "control",
        "timestampUs": event.timestamp_us,
        "source": {
            SOURCE_PHYSICAL: "physical", SOURCE_REMOTE: "remote"
        }.get(event.source, event.source),
        "control": control.name,
        "keyCode": f"0x{control.key_code:04x}",
        "operation": OPERATION_NAMES.get(control.operation, control.operation),
        "operationCode": control.operation,
        "channel": control.channel,
        "value": control.value,
        "floatValue": control.float_value,
        "floatBits": f"0x{control.float_bits:08x}",
        "auxiliary": control.auxiliary,
        "flags": event.flags,
    }


def list_controls(pattern: str | None) -> None:
    for control in CONTROLS:
        if pattern and pattern.lower() not in control.name.lower():
            continue
        raw_range = (
            f" {control.raw_min}..{control.raw_max}"
            if control.raw_min is not None else ""
        )
        print(
            f"{control.name:<30} 0x{control.key_code:04x} "
            f"{control.scope:<7} {control.kind}{raw_range}"
        )


def listen(args: argparse.Namespace) -> None:
    for handshake, event in reconnecting_events(
        args.host, args.port, reconnect_delay=args.reconnect_delay
    ):
        if event is None:
            print(json.dumps({
                "type": "connected",
                "firmware": (
                    f"{handshake.hello.firmware_major}."
                    f"{handshake.hello.firmware_minor:02d}"
                ),
                "rbpBuildPrefix": handshake.hello.rbp_build_prefix.hex(),
                "schemaRevision": handshake.hello.schema_revision,
                "schemaCrc32": f"0x{handshake.schema_crc32:08x}",
                "controlCount": handshake.control_count,
            }), flush=True)
            continue
        print(json.dumps(event_dict(event), separators=(",", ":")), flush=True)


def send_commands(
    args: argparse.Namespace,
    commands: list[ControlTuple],
    *,
    inter_command_delay: float = 0.0,
) -> None:
    random_source = random.SystemRandom()
    responses = []
    with Connection.connect(args.host, args.port, args.timeout) as connection:
        connection.handshake()
        for index, command in enumerate(commands):
            request_id = random_source.randrange(1, 1 << 32)
            result = connection.command(command, request_id)
            responses.append({"requestId": request_id, "result": result})
            if index + 1 < len(commands) and inter_command_delay:
                time.sleep(inter_command_delay)
    print(json.dumps({"type": "ack", "responses": responses}))


def raw_command(args: argparse.Namespace) -> None:
    send_commands(args, [ControlTuple.create(
        args.control, operation=args.operation, channel=args.channel,
        value=args.value, float_value=args.float_value, auxiliary=args.auxiliary,
    )])


def press(args: argparse.Namespace) -> None:
    spec = resolve_control(args.control)
    if spec.kind != "button":
        raise ValueError(f"{spec.name} is {spec.kind}, not a button")
    send_commands(
        args,
        [
            ControlTuple.create(
                spec.name, operation=OP_PRESSED, channel=args.channel
            ),
            ControlTuple.create(
                spec.name, operation=OP_RELEASED, channel=args.channel
            ),
        ],
        inter_command_delay=0.03,
    )


def set_absolute(args: argparse.Namespace) -> None:
    spec = resolve_control(args.control)
    if spec.raw_min != 0 or spec.raw_max != 1023:
        raise ValueError(f"{spec.name} does not have a verified 0..1023 range")
    if not 0 <= args.value <= 1023:
        raise ValueError("value must be between 0 and 1023")
    send_commands(args, [ControlTuple.create(
        spec.name, operation=OP_ABSOLUTE_MOVED, channel=args.channel,
        value=args.value, float_value=args.value / 1023.0,
    )])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    subparsers = parser.add_subparsers(dest="action", required=True)

    controls_parser = subparsers.add_parser("controls")
    controls_parser.add_argument("pattern", nargs="?")

    listen_parser = subparsers.add_parser("listen")
    listen_parser.add_argument("--reconnect-delay", type=float, default=1.0)

    command_parser = subparsers.add_parser("command")
    command_parser.add_argument("control")
    command_parser.add_argument("--channel", required=True, type=int)
    command_parser.add_argument("--operation", required=True, type=parse_operation)
    command_parser.add_argument("--value", type=int, default=0)
    command_parser.add_argument("--float-value", type=float, default=0.0)
    command_parser.add_argument("--auxiliary", type=int, default=0)
    command_parser.add_argument("--timeout", type=float, default=5.0)

    press_parser = subparsers.add_parser("press")
    press_parser.add_argument("control")
    press_parser.add_argument("--channel", required=True, type=int)
    press_parser.add_argument("--timeout", type=float, default=5.0)

    set_parser = subparsers.add_parser("set")
    set_parser.add_argument("control")
    set_parser.add_argument("value", type=int)
    set_parser.add_argument("--channel", required=True, type=int)
    set_parser.add_argument("--timeout", type=float, default=5.0)

    args = parser.parse_args()
    try:
        if args.action == "controls":
            list_controls(args.pattern)
        elif args.action == "listen":
            listen(args)
        elif args.action == "press":
            press(args)
        elif args.action == "set":
            set_absolute(args)
        else:
            raw_command(args)
    except (ConnectionError, KeyError, OSError, ProtocolError, RemoteError, ValueError) as error:
        print(f"rx3-remote: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
