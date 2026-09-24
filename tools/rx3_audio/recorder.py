"""Record a reconnecting RX3 PCM stream as a seekable WAV file."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import logging
from pathlib import Path
import signal
import socket
import struct
import time
from typing import BinaryIO

from .protocol import CONFIG, HEARTBEAT, PCM, Decoder, Message, ProtocolError, StreamConfig


LOG = logging.getLogger("rx3-audio-recorder")
WAV_HEADER = struct.Struct("<4sI4s4sIHHIIHH4sI")
UINT32_MAX = (1 << 32) - 1


class WavWriter:
    """Write PCM directly and keep RIFF lengths valid after every flush."""

    def __init__(self, path: Path, config: StreamConfig) -> None:
        self._file: BinaryIO = path.open("w+b", buffering=0)
        self.config = config
        self.data_bytes = 0
        self._write_header()

    def _write_header(self) -> None:
        if self.data_bytes > UINT32_MAX - WAV_HEADER.size:
            raise OverflowError("recording exceeds the 4 GiB RIFF/WAV limit")
        config = self.config
        self._file.seek(0)
        self._file.write(
            WAV_HEADER.pack(
                b"RIFF",
                36 + self.data_bytes,
                b"WAVE",
                b"fmt ",
                16,
                1,
                config.channels,
                config.sample_rate,
                config.sample_rate * config.block_align,
                config.block_align,
                config.sample_width * 8,
                b"data",
                self.data_bytes,
            )
        )
        self._file.seek(WAV_HEADER.size + self.data_bytes)

    def write(self, pcm: bytes) -> None:
        if len(pcm) % self.config.block_align:
            raise ProtocolError("PCM payload is not aligned to a complete sample frame")
        self._file.write(pcm)
        self.data_bytes += len(pcm)

    def flush(self) -> None:
        self._write_header()
        self._file.flush()

    def close(self) -> None:
        if not self._file.closed:
            self.flush()
            self._file.close()

    def __enter__(self) -> WavWriter:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()


@dataclass
class Stats:
    started_at: float
    bytes_written: int = 0
    blocks: int = 0
    connections: int = 0
    sequence_gaps: int = 0
    missing_frames: int = 0
    discontinuities: int = 0
    sender_dropped_frames: int = 0

    def line(self, config: StreamConfig) -> str:
        elapsed = max(time.monotonic() - self.started_at, 0.001)
        seconds = self.bytes_written / config.block_align / config.sample_rate
        rate_kbit = self.bytes_written * 8 / elapsed / 1000
        return (
            f"recorded={seconds:.1f}s rate={rate_kbit:.0f}kbit/s "
            f"blocks={self.blocks} connections={self.connections} "
            f"seq_gaps={self.sequence_gaps} missing_frames={self.missing_frames} "
            f"sender_drops={self.sender_dropped_frames}"
        )


class StreamRecorder:
    def __init__(self, output: Path, stats_interval: float = 1.0) -> None:
        self.output = output
        self.stats_interval = stats_interval
        self.config: StreamConfig | None = None
        self.writer: WavWriter | None = None
        self.stats = Stats(time.monotonic())
        self._expected_sequence: int | None = None
        self._expected_timestamp: int | None = None
        self._sender_drop_count: int | None = None
        self._next_stats_at = time.monotonic() + stats_interval

    def begin_connection(self) -> None:
        self.stats.connections += 1
        self._expected_sequence = None
        self._expected_timestamp = None
        self._sender_drop_count = None

    def handle(self, message: Message) -> None:
        if message.type == CONFIG:
            incoming = StreamConfig.decode(message.payload)
            if self.config is not None and incoming != self.config:
                raise ProtocolError(f"stream format changed from {self.config} to {incoming}")
            if self.writer is None:
                self.config = incoming
                self.writer = WavWriter(self.output, incoming)
                LOG.info(
                    "format %d Hz, %d channel(s), signed 16-bit little-endian",
                    incoming.sample_rate,
                    incoming.channels,
                )
            self._expected_sequence = (message.sequence + 1) & 0xFFFFFFFF
            self._expected_timestamp = message.timestamp_frames
            self._sender_drop_count = message.dropped_frames
            return

        if message.type == HEARTBEAT:
            self._track_sequence_and_drops(message)
            self._expected_sequence = (message.sequence + 1) & 0xFFFFFFFF
            return
        if self.writer is None or self.config is None:
            raise ProtocolError("PCM arrived before stream config")

        self._track_continuity(message)
        self.writer.write(message.payload)
        frame_count = len(message.payload) // self.config.block_align
        self._expected_sequence = (message.sequence + 1) & 0xFFFFFFFF
        self._expected_timestamp = message.timestamp_frames + frame_count
        self.stats.bytes_written += len(message.payload)
        self.stats.blocks += 1

        now = time.monotonic()
        if now >= self._next_stats_at:
            self.writer.flush()
            LOG.info(self.stats.line(self.config))
            self._next_stats_at = now + self.stats_interval

    def _track_continuity(self, message: Message) -> None:
        self._track_sequence_and_drops(message)

        if self._expected_timestamp is not None:
            if message.timestamp_frames > self._expected_timestamp:
                self.stats.missing_frames += message.timestamp_frames - self._expected_timestamp
            elif message.timestamp_frames < self._expected_timestamp:
                self.stats.discontinuities += 1

    def _track_sequence_and_drops(self, message: Message) -> None:
        if self._expected_sequence is not None and message.sequence != self._expected_sequence:
            gap = (message.sequence - self._expected_sequence) & 0xFFFFFFFF
            self.stats.sequence_gaps += gap if gap < 0x80000000 else 1

        if self._sender_drop_count is not None:
            dropped = (message.dropped_frames - self._sender_drop_count) & 0xFFFFFFFF
            if dropped < 0x80000000:
                self.stats.sender_dropped_frames += dropped
        self._sender_drop_count = message.dropped_frames

    def close(self) -> None:
        if self.writer is not None:
            self.writer.close()


def record(
    host: str,
    port: int,
    output: Path,
    reconnect_delay: float = 1.0,
    receive_buffer: int = 256 * 1024,
) -> None:
    recorder = StreamRecorder(output)
    stopping = False

    def stop(*_: object) -> None:
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    try:
        while not stopping:
            try:
                with socket.create_connection((host, port), timeout=5) as connection:
                    connection.settimeout(1)
                    connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
                    recorder.begin_connection()
                    decoder = Decoder()
                    LOG.info("connected to %s:%d", host, port)

                    while not stopping:
                        try:
                            data = connection.recv(64 * 1024)
                        except TimeoutError:
                            continue
                        if not data:
                            raise ConnectionError("stream closed")
                        for message in decoder.feed(data):
                            recorder.handle(message)
            except (ConnectionError, OSError, ProtocolError) as error:
                if not stopping:
                    LOG.warning("stream unavailable: %s; reconnecting", error)
                    time.sleep(reconnect_delay)
    finally:
        recorder.close()
        if recorder.config is not None:
            LOG.info("finished: %s", recorder.stats.line(recorder.config))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="169.254.100.2")
    parser.add_argument("--port", type=int, default=7355)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reconnect-delay", type=float, default=1.0)
    parser.add_argument("--receive-buffer", type=int, default=256 * 1024)
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    record(args.host, args.port, args.output, args.reconnect_delay, args.receive_buffer)


if __name__ == "__main__":
    main()
