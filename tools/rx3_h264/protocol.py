"""Framing for access units produced by the RX3 H.264 streamer."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Iterator


MAGIC = b"RX3H"
VERSION = 1

CONFIG = 1
ACCESS_UNIT = 2
HEARTBEAT = 3

FLAG_KEYFRAME = 1 << 0

HEADER = struct.Struct("!4sBBHIQI")
MAX_PAYLOAD = 4 * 1024 * 1024


class ProtocolError(ValueError):
    """The RX3 sent an invalid H.264 stream message."""


@dataclass(frozen=True)
class Message:
    type: int
    flags: int
    sequence: int
    timestamp_us: int
    payload: bytes

    @property
    def is_keyframe(self) -> bool:
        return bool(self.flags & FLAG_KEYFRAME)

    def encode(self) -> bytes:
        return encode_message(
            self.type,
            self.sequence,
            self.timestamp_us,
            self.payload,
            self.flags,
        )


def encode_message(
    message_type: int,
    sequence: int,
    timestamp_us: int,
    payload: bytes = b"",
    flags: int = 0,
) -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError(f"payload is too large: {len(payload)} bytes")

    return HEADER.pack(
        MAGIC,
        VERSION,
        message_type,
        flags,
        sequence,
        timestamp_us,
        len(payload),
    ) + payload


class Decoder:
    """Decode arbitrarily chunked TCP data into complete stream messages."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> Iterator[Message]:
        self._buffer.extend(data)

        while len(self._buffer) >= HEADER.size:
            magic, version, message_type, flags, sequence, timestamp_us, length = (
                HEADER.unpack_from(self._buffer)
            )
            if magic != MAGIC:
                raise ProtocolError("message does not start with RX3H")
            if version != VERSION:
                raise ProtocolError(f"unsupported RX3H version {version}")
            if message_type not in (CONFIG, ACCESS_UNIT, HEARTBEAT):
                raise ProtocolError(f"unsupported RX3H message type {message_type}")
            if length > MAX_PAYLOAD:
                raise ProtocolError(f"payload is too large: {length} bytes")

            size = HEADER.size + length
            if len(self._buffer) < size:
                return

            payload = bytes(self._buffer[HEADER.size:size])
            del self._buffer[:size]
            yield Message(message_type, flags, sequence, timestamp_us, payload)
