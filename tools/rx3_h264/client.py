"""Receive the RX3 framed TCP stream and write Annex-B H.264."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import socket
import sys
from typing import BinaryIO, Protocol

from .protocol import ACCESS_UNIT, CONFIG, HEARTBEAT, Decoder, ProtocolError


class Receiver(Protocol):
    def recv(self, size: int) -> bytes: ...


@dataclass(frozen=True)
class StreamStats:
    frames: int
    keyframes: int
    bytes_written: int
    first_timestamp_us: int | None
    last_timestamp_us: int | None


def copy_stream(
    receiver: Receiver,
    output: BinaryIO,
    *,
    frame_limit: int | None = None,
) -> StreamStats:
    """Copy one framed TCP connection to an Annex-B byte stream."""
    decoder = Decoder()
    expected_sequence: int | None = None
    configured = False
    frames = 0
    keyframes = 0
    bytes_written = 0
    first_timestamp_us: int | None = None
    last_timestamp_us: int | None = None

    while frame_limit is None or frames < frame_limit:
        data = receiver.recv(256 * 1024)
        if not data:
            break

        for message in decoder.feed(data):
            if expected_sequence is not None and message.sequence != expected_sequence:
                raise ProtocolError(
                    f"sequence gap: expected {expected_sequence}, got {message.sequence}"
                )
            expected_sequence = (message.sequence + 1) & 0xFFFFFFFF

            if message.type == HEARTBEAT:
                continue
            if message.type == CONFIG:
                configured = True
            elif not configured:
                raise ProtocolError("received an access unit before SPS/PPS")

            output.write(message.payload)
            bytes_written += len(message.payload)

            if message.type != ACCESS_UNIT:
                continue

            frames += 1
            keyframes += int(message.is_keyframe)
            first_timestamp_us = (
                message.timestamp_us
                if first_timestamp_us is None
                else first_timestamp_us
            )
            last_timestamp_us = message.timestamp_us
            if frame_limit is not None and frames >= frame_limit:
                break

    output.flush()
    return StreamStats(
        frames,
        keyframes,
        bytes_written,
        first_timestamp_us,
        last_timestamp_us,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="169.254.100.2")
    parser.add_argument("--port", type=int, default=7353)
    parser.add_argument(
        "--output",
        default="-",
        help="Annex-B output path, or - for stdout (default: -)",
    )
    parser.add_argument(
        "--frames",
        type=int,
        help="stop after this many access units instead of reading continuously",
    )
    parser.add_argument("--connect-timeout", type=float, default=5)
    args = parser.parse_args()
    if args.frames is not None and args.frames < 1:
        parser.error("--frames must be positive")
    return args


def main() -> None:
    args = parse_args()
    output = sys.stdout.buffer if args.output == "-" else open(args.output, "wb")

    try:
        with socket.create_connection(
            (args.host, args.port),
            timeout=args.connect_timeout,
        ) as connection:
            connection.settimeout(None)
            stats = copy_stream(connection, output, frame_limit=args.frames)
    finally:
        if output is not sys.stdout.buffer:
            output.close()

    duration_us = (
        0
        if stats.first_timestamp_us is None or stats.last_timestamp_us is None
        else stats.last_timestamp_us - stats.first_timestamp_us
    )
    rate = 0 if duration_us <= 0 else (stats.frames - 1) * 1_000_000 / duration_us
    print(
        f"received {stats.frames} frames ({stats.keyframes} keyframes), "
        f"{stats.bytes_written} bytes, {rate:.2f} fps",
        file=sys.stderr,
    )


if __name__ == "__main__":
    main()
