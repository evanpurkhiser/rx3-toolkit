#!/usr/bin/env python3
"""Capture one reconstructed RX3 display frame as a PNG."""

from __future__ import annotations

import argparse
import binascii
from pathlib import Path
import select
import socket
import struct
import time
import zlib

from .cli import DEFAULT_HOST, DEFAULT_PORT
from .protocol import Decoder, FRAME, HELLO, Framebuffer, ProtocolError, decode_hello


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack("!I", len(payload)) + body + struct.pack(
        "!I", binascii.crc32(body) & 0xFFFFFFFF
    )


def write_png(output: Path, framebuffer: Framebuffer) -> None:
    info = framebuffer.info
    rows = bytearray()
    pixels = framebuffer.pixels
    for y in range(info.height):
        rows.append(0)
        offset = y * info.stride
        for x in range(info.width):
            value = pixels[offset] | pixels[offset + 1] << 8
            offset += 2
            red = (value >> 11) & 0x1F
            green = (value >> 5) & 0x3F
            blue = value & 0x1F
            rows.extend((red * 255 // 31, green * 255 // 63, blue * 255 // 31))

    header = struct.pack("!IIBBBBB", info.width, info.height, 8, 2, 0, 0, 0)
    output.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(rows, 6))
        + png_chunk(b"IEND", b"")
    )


def capture(host: str, port: int, output: Path, settle: float) -> None:
    decoder = Decoder()
    framebuffer: Framebuffer | None = None
    synchronized = False
    deadline: float | None = None

    with socket.create_connection((host, port), timeout=5) as connection:
        connection.setblocking(False)
        while deadline is None or time.monotonic() < deadline:
            timeout = 5.0 if deadline is None else max(0.0, deadline - time.monotonic())
            readable, _, _ = select.select([connection], [], [], timeout)
            if not readable:
                if deadline is None:
                    raise TimeoutError("RX3 did not send a framebuffer keyframe")
                break

            data = connection.recv(256 * 1024)
            if not data:
                raise ConnectionError("RX3 closed the framebuffer stream")

            for message in decoder.feed(data):
                if message.type == HELLO:
                    framebuffer = Framebuffer(decode_hello(message.payload))
                    continue
                if message.type != FRAME or framebuffer is None:
                    continue
                if message.is_keyframe:
                    synchronized = True
                    deadline = time.monotonic() + settle
                if synchronized:
                    framebuffer.apply(message)

    if framebuffer is None or not synchronized:
        raise ProtocolError("RX3 stream ended before a complete frame")

    output.parent.mkdir(parents=True, exist_ok=True)
    write_png(output, framebuffer)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--settle", type=float, default=0.25)
    arguments = parser.parse_args()
    capture(
        arguments.host,
        arguments.port,
        arguments.output,
        max(0.0, arguments.settle),
    )


if __name__ == "__main__":
    main()
