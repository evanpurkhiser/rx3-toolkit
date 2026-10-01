"""Framing shared by the RX3 PCM sender and host recorder."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Iterator


MAGIC = b"RX3A"
VERSION = 1

CONFIG = 1
PCM = 2

S16_LE = 1

# magic, version, type, flags, sequence, payload bytes, sample-frame timestamp,
# and the sender's cumulative count of dropped sample frames.
HEADER = struct.Struct("!4sBBHIIQI")
CONFIG_PAYLOAD = struct.Struct("!IHHI")

MAX_PAYLOAD = 1024 * 1024
MAX_BUFFER = HEADER.size + MAX_PAYLOAD


class ProtocolError(ValueError):
    """The RX3 sent an invalid audio stream message."""


@dataclass(frozen=True)
class StreamConfig:
    sample_rate: int
    channels: int
    sample_format: int
    max_frames_per_block: int

    @property
    def sample_width(self) -> int:
        if self.sample_format == S16_LE:
            return 2
        raise ProtocolError(f"unsupported sample format {self.sample_format}")

    @property
    def block_align(self) -> int:
        return self.channels * self.sample_width

    def encode(self) -> bytes:
        self.validate()
        return CONFIG_PAYLOAD.pack(
            self.sample_rate,
            self.channels,
            self.sample_format,
            self.max_frames_per_block,
        )

    def validate(self) -> None:
        if not 8_000 <= self.sample_rate <= 384_000:
            raise ProtocolError(f"invalid sample rate {self.sample_rate}")
        if not 1 <= self.channels <= 32:
            raise ProtocolError(f"invalid channel count {self.channels}")
        if self.sample_format != S16_LE:
            raise ProtocolError(f"unsupported sample format {self.sample_format}")
        if not 1 <= self.max_frames_per_block <= self.sample_rate:
            raise ProtocolError(
                f"invalid maximum frames per block {self.max_frames_per_block}"
            )

    @classmethod
    def decode(cls, payload: bytes) -> StreamConfig:
        if len(payload) != CONFIG_PAYLOAD.size:
            raise ProtocolError(f"invalid config length {len(payload)}")
        config = cls(*CONFIG_PAYLOAD.unpack(payload))
        config.validate()
        return config


@dataclass(frozen=True)
class Message:
    type: int
    flags: int
    sequence: int
    timestamp_frames: int
    dropped_frames: int
    payload: bytes

    def encode(self) -> bytes:
        return encode_message(
            self.type,
            self.sequence,
            self.timestamp_frames,
            self.dropped_frames,
            self.payload,
            self.flags,
        )


def encode_message(
    message_type: int,
    sequence: int,
    timestamp_frames: int,
    dropped_frames: int,
    payload: bytes = b"",
    flags: int = 0,
) -> bytes:
    if message_type not in (CONFIG, PCM):
        raise ProtocolError(f"unsupported RX3A message type {message_type}")
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError(f"payload is too large: {len(payload)} bytes")

    return HEADER.pack(
        MAGIC,
        VERSION,
        message_type,
        flags,
        sequence & 0xFFFFFFFF,
        len(payload),
        timestamp_frames,
        dropped_frames,
    ) + payload


class Decoder:
    """Decode arbitrarily chunked TCP data with a strict memory bound."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> Iterator[Message]:
        if len(self._buffer) + len(data) > MAX_BUFFER:
            raise ProtocolError("receive buffer limit exceeded")
        self._buffer.extend(data)

        while len(self._buffer) >= HEADER.size:
            (
                magic,
                version,
                message_type,
                flags,
                sequence,
                length,
                timestamp_frames,
                dropped_frames,
            ) = HEADER.unpack_from(self._buffer)
            if magic != MAGIC:
                raise ProtocolError("message does not start with RX3A")
            if version != VERSION:
                raise ProtocolError(f"unsupported RX3A version {version}")
            if message_type not in (CONFIG, PCM):
                raise ProtocolError(f"unsupported RX3A message type {message_type}")
            if length > MAX_PAYLOAD:
                raise ProtocolError(f"payload is too large: {length} bytes")

            size = HEADER.size + length
            if len(self._buffer) < size:
                return

            payload = bytes(self._buffer[HEADER.size:size])
            del self._buffer[:size]
            yield Message(
                message_type,
                flags,
                sequence,
                timestamp_frames,
                dropped_frames,
                payload,
            )
