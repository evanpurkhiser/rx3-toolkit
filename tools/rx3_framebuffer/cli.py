#!/usr/bin/env python3
"""Receive an RX3 dirty-rectangle framebuffer stream and display it with ffplay."""

from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import time

from .protocol import Decoder, FRAME, HELLO, Framebuffer, ProtocolError, decode_hello
from .web import ViewerHub, start_server


DEFAULT_HOST = "169.254.100.2"
DEFAULT_PORT = 7351


def start_ffplay(width: int, height: int, frame_rate: int) -> subprocess.Popen[bytes]:
    executable = shutil.which("ffplay")
    if executable is None:
        raise RuntimeError("ffplay is not installed or is not on PATH")

    return subprocess.Popen(
        [
            executable,
            "-loglevel", "warning",
            "-f", "rawvideo",
            "-pixel_format", "rgb565le",
            "-video_size", f"{width}x{height}",
            "-framerate", str(frame_rate),
            "-i", "-",
            "-vf", "scale=iw:ih:flags=neighbor",
            "-window_title", "RX3 display",
        ],
        stdin=subprocess.PIPE,
    )


def visible_frame(framebuffer: Framebuffer) -> bytes:
    """Remove any row padding before feeding a packed raw-video consumer."""
    info = framebuffer.info
    visible_stride = info.width * 2
    if info.stride == visible_stride:
        return bytes(framebuffer.pixels)

    return b"".join(
        framebuffer.pixels[row * info.stride:row * info.stride + visible_stride]
        for row in range(info.height)
    )


class Stats:
    def __init__(self) -> None:
        self.started = time.monotonic()
        self.last_report = self.started
        self.frames = 0
        self.bytes = 0
        self.rectangles = 0

    def add(self, wire_bytes: int, rectangles: int) -> None:
        self.frames += 1
        self.bytes += wire_bytes
        self.rectangles += rectangles

        now = time.monotonic()
        elapsed = now - self.last_report
        if elapsed < 1:
            return

        total = now - self.started
        print(
            f"{self.frames / total:5.1f} fps  "
            f"{self.bytes * 8 / total / 1_000_000:6.2f} Mbit/s  "
            f"{self.rectangles / self.frames:5.1f} rects/frame",
            file=sys.stderr,
            flush=True,
        )
        self.last_report = now


def receive(host: str, port: int, *, display: bool, frame_rate: int,
            web_port: int | None = None) -> None:
    decoder = Decoder()
    framebuffer: Framebuffer | None = None
    player: subprocess.Popen[bytes] | None = None
    stats = Stats()
    synchronized = False
    hub = ViewerHub()
    web_server = start_server(hub, web_port) if web_port is not None else None
    if web_server is not None:
        print(
            f"web viewer: http://127.0.0.1:{web_port}/ "
            f"(tailnet: https://{web_port}.prk.network/)",
            file=sys.stderr,
        )

    with socket.create_connection((host, port), timeout=5) as connection:
        connection.settimeout(None)
        connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"connected to {host}:{port}", file=sys.stderr)

        try:
            while data := connection.recv(64 * 1024):
                for message in decoder.feed(data):
                    if message.type == HELLO:
                        info = decode_hello(message.payload)
                        framebuffer = Framebuffer(info)
                        if web_server is not None:
                            framebuffer = hub.configure(info)
                        print(
                            f"stream is {info.width}x{info.height}, "
                            f"stride {info.stride}, RGB565LE, tiles {info.tile_size}px",
                            file=sys.stderr,
                        )
                        if display:
                            player = start_ffplay(info.width, info.height, frame_rate)
                        continue

                    if message.type != FRAME:
                        continue
                    if framebuffer is None:
                        raise ProtocolError("received a FRAME before HELLO")

                    previous_sequence = framebuffer.last_sequence
                    expected_sequence = (
                        (previous_sequence + 1) & 0xFFFFFFFF
                        if previous_sequence is not None else message.sequence
                    )
                    if message.sequence != expected_sequence and not message.is_keyframe:
                        synchronized = False
                        print(
                            f"sequence gap: expected {expected_sequence}, "
                            f"received {message.sequence}; awaiting keyframe",
                            file=sys.stderr,
                        )
                    if message.is_keyframe:
                        synchronized = True
                    if not synchronized:
                        framebuffer.last_sequence = message.sequence
                        continue

                    if web_server is not None:
                        update, framebuffer = hub.apply_and_publish(message)
                    else:
                        update = framebuffer.apply(message)
                    stats.add(len(message.payload) + 16, update.rectangles)
                    if player is not None and player.stdin is not None:
                        player.stdin.write(visible_frame(framebuffer))
                        player.stdin.flush()
        finally:
            if player is not None:
                if player.stdin is not None:
                    player.stdin.close()
                player.terminate()
                player.wait(timeout=2)
            if web_server is not None:
                web_server.shutdown()
                web_server.server_close()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Receive the RX3 RGB565 dirty-rectangle framebuffer stream"
    )
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--fps", type=int, default=30,
                        help="ffplay input rate (default: 30)")
    parser.add_argument("--no-display", action="store_true",
                        help="receive and report statistics without opening ffplay")
    parser.add_argument("--web-port", type=int,
                        help="serve a phone-friendly canvas viewer on 127.0.0.1")
    arguments = parser.parse_args()

    try:
        receive(
            arguments.host,
            arguments.port,
            display=not arguments.no_display,
            frame_rate=arguments.fps,
            web_port=arguments.web_port,
        )
    except (ConnectionError, OSError, ProtocolError, RuntimeError) as error:
        print(f"rx3-framebuffer: {error}", file=sys.stderr)
        raise SystemExit(1) from error
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
