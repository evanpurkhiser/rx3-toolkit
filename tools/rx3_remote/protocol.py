"""Bounded binary framing for the RX3 bidirectional control protocol."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import Iterator

from .controls import BY_CODE, resolve_control


MAGIC = b"RX3R"
VERSION = 1

HELLO = 1
SCHEMA = 2
SUBSCRIBE = 3
EVENT = 4
COMMAND = 5
ACK = 6
ERROR = 7
PING = 8
PONG = 9

MESSAGE_NAMES = {
    HELLO: "hello", SCHEMA: "schema", SUBSCRIBE: "subscribe",
    EVENT: "event", COMMAND: "command", ACK: "ack", ERROR: "error",
    PING: "ping", PONG: "pong",
}

CAP_EVENTS = 1 << 0
CAP_COMMANDS = 1 << 1
CAP_SCHEMA = 1 << 2
EVENT_CONTROLS = 1 << 0
EVENT_ALL = EVENT_CONTROLS

SOURCE_PHYSICAL = 1
SOURCE_REMOTE = 2

class Operation(IntEnum):
    PRESSED = 0
    LONG_PRESSED = 1
    RELEASED = 2
    RELEASED_AFTER_LONG_PRESS = 3
    RELATIVE_MOVED = 4
    ABSOLUTE_MOVED = 5
    VALUE_CHANGED = 6


OP_PRESSED = Operation.PRESSED
OP_LONG_PRESSED = Operation.LONG_PRESSED
OP_RELEASED = Operation.RELEASED
OP_RELEASED_AFTER_LONG_PRESS = Operation.RELEASED_AFTER_LONG_PRESS
OP_RELATIVE_MOVED = Operation.RELATIVE_MOVED
OP_ABSOLUTE_MOVED = Operation.ABSOLUTE_MOVED
OP_VALUE_CHANGED = Operation.VALUE_CHANGED
OPERATION_NAMES = {
    OP_PRESSED: "pressed",
    OP_LONG_PRESSED: "long_pressed",
    OP_RELEASED: "released",
    OP_RELEASED_AFTER_LONG_PRESS: "released_after_long_press",
    OP_RELATIVE_MOVED: "relative_moved",
    OP_ABSOLUTE_MOVED: "absolute_moved",
    OP_VALUE_CHANGED: "value_changed",
}

ERROR_INVALID_MESSAGE = 1
ERROR_INVALID_COMMAND = 2
ERROR_UNSUPPORTED_CONTROL = 3
ERROR_BUSY = 4
ERROR_INTERNAL = 5
ERROR_NAMES = {
    ERROR_INVALID_MESSAGE: "invalid message",
    ERROR_INVALID_COMMAND: "invalid command",
    ERROR_UNSUPPORTED_CONTROL: "unsupported control",
    ERROR_BUSY: "busy",
    ERROR_INTERNAL: "internal error",
}

# magic, protocol version, type, flags, request ID, payload length
HEADER = struct.Struct("!4sBBHII")
HELLO_PAYLOAD = struct.Struct("!4sHHII")
SCHEMA_PAYLOAD = struct.Struct("!II")
SUBSCRIBE_PAYLOAD = struct.Struct("!I")
CONTROL_PAYLOAD = struct.Struct("!HBBiIi")
EVENT_SUFFIX = struct.Struct("!BBHQ")
ACK_PAYLOAD = struct.Struct("!I")
ERROR_PAYLOAD = struct.Struct("!II")
KEEPALIVE_PAYLOAD = struct.Struct("!Q")

MAX_PAYLOAD = 4096
MAX_BUFFER = 256 * 1024


class ProtocolError(ValueError):
    """A peer sent an invalid remote-control message."""


@dataclass(frozen=True)
class Message:
    type: int
    flags: int
    request_id: int
    payload: bytes

    @property
    def name(self) -> str:
        return MESSAGE_NAMES[self.type]

    def encode(self) -> bytes:
        return encode_message(self.type, self.payload, self.request_id, self.flags)


@dataclass(frozen=True)
class Hello:
    rbp_build_prefix: bytes
    firmware_major: int
    firmware_minor: int
    schema_revision: int
    capabilities: int

    def encode(self) -> bytes:
        if len(self.rbp_build_prefix) != 4:
            raise ProtocolError("rbp build prefix must be exactly four bytes")
        return HELLO_PAYLOAD.pack(
            self.rbp_build_prefix, self.firmware_major, self.firmware_minor,
            self.schema_revision, self.capabilities,
        )

    @classmethod
    def decode(cls, payload: bytes) -> Hello:
        return cls(*_unpack_exact(HELLO_PAYLOAD, payload, "hello"))


@dataclass(frozen=True)
class ControlTuple:
    key_code: int
    operation: int
    channel: int
    value: int = 0
    float_bits: int = 0
    auxiliary: int = 0

    def __post_init__(self) -> None:
        if not 0 <= self.key_code <= 0xFFFF:
            raise ValueError("key code must fit an unsigned 16-bit integer")
        if not 0 <= self.operation <= 15:
            raise ValueError("operation must fit the KeyInput operation nibble")
        if not 0 <= self.channel <= 255:
            raise ValueError("channel must be between 0 and 255")
        if not -(1 << 31) <= self.value < (1 << 31):
            raise ValueError("value must fit a signed 32-bit integer")
        if not 0 <= self.float_bits <= 0xFFFFFFFF:
            raise ValueError("float bits must fit an unsigned 32-bit integer")
        if not -(1 << 31) <= self.auxiliary < (1 << 31):
            raise ValueError("auxiliary must fit a signed 32-bit integer")

    @property
    def name(self) -> str:
        control = BY_CODE.get(self.key_code)
        return control.name if control else f"key_0x{self.key_code:04x}"

    @property
    def float_value(self) -> float:
        return struct.unpack("!f", self.float_bits.to_bytes(4, "big"))[0]

    def encode(self) -> bytes:
        return CONTROL_PAYLOAD.pack(
            self.key_code, self.operation, self.channel, self.value,
            self.float_bits, self.auxiliary,
        )

    @classmethod
    def decode(cls, payload: bytes) -> ControlTuple:
        return cls(*_unpack_exact(CONTROL_PAYLOAD, payload, "control"))

    @classmethod
    def create(
        cls, control: str | int, *, operation: int, channel: int,
        value: int = 0, float_value: float = 0.0, auxiliary: int = 0,
    ) -> ControlTuple:
        spec = resolve_control(control)
        float_bits = int.from_bytes(struct.pack("!f", float_value), "big")
        return cls(spec.key_code, operation, channel, value, float_bits, auxiliary)


@dataclass(frozen=True)
class ControlEvent:
    control: ControlTuple
    source: int
    flags: int
    timestamp_us: int

    def encode(self) -> bytes:
        return self.control.encode() + EVENT_SUFFIX.pack(
            self.source, self.flags, 0, self.timestamp_us
        )

    @classmethod
    def decode(cls, payload: bytes) -> ControlEvent:
        expected = CONTROL_PAYLOAD.size + EVENT_SUFFIX.size
        if len(payload) != expected:
            raise ProtocolError(
                f"invalid control event length {len(payload)}; expected {expected}"
            )
        control = ControlTuple.decode(payload[:CONTROL_PAYLOAD.size])
        source, flags, reserved, timestamp_us = EVENT_SUFFIX.unpack_from(
            payload, CONTROL_PAYLOAD.size
        )
        if reserved:
            raise ProtocolError("control event reserved field is nonzero")
        return cls(control, source, flags, timestamp_us)


def _unpack_exact(
    payload_struct: struct.Struct, payload: bytes, label: str
) -> tuple[object, ...]:
    if len(payload) != payload_struct.size:
        raise ProtocolError(
            f"invalid {label} length {len(payload)}; expected {payload_struct.size}"
        )
    return payload_struct.unpack(payload)


def encode_message(
    message_type: int, payload: bytes = b"", request_id: int = 0, flags: int = 0,
) -> bytes:
    if message_type not in MESSAGE_NAMES:
        raise ProtocolError(f"unsupported RX3R message type {message_type}")
    if not 0 <= request_id <= 0xFFFFFFFF:
        raise ProtocolError(f"invalid request ID {request_id}")
    if len(payload) > MAX_PAYLOAD:
        raise ProtocolError(f"payload is too large: {len(payload)} bytes")
    if message_type == COMMAND and request_id == 0:
        raise ProtocolError("commands require a nonzero request ID")

    return HEADER.pack(
        MAGIC, VERSION, message_type, flags, request_id, len(payload)
    ) + payload


class Decoder:
    """Decode arbitrarily chunked TCP data while enforcing a memory limit."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> Iterator[Message]:
        if len(self._buffer) + len(data) > MAX_BUFFER:
            raise ProtocolError("receive buffer limit exceeded")
        self._buffer.extend(data)

        while len(self._buffer) >= HEADER.size:
            magic, version, message_type, flags, request_id, length = HEADER.unpack_from(
                self._buffer
            )
            if magic != MAGIC:
                raise ProtocolError("message does not start with RX3R")
            if version != VERSION:
                raise ProtocolError(f"unsupported RX3R version {version}")
            if message_type not in MESSAGE_NAMES:
                raise ProtocolError(f"unsupported RX3R message type {message_type}")
            if length > MAX_PAYLOAD:
                raise ProtocolError(f"payload is too large: {length} bytes")

            size = HEADER.size + length
            if len(self._buffer) < size:
                return

            payload = bytes(self._buffer[HEADER.size:size])
            del self._buffer[:size]
            yield Message(message_type, flags, request_id, payload)


def encode_schema(crc32: int, count: int) -> bytes:
    return SCHEMA_PAYLOAD.pack(crc32, count)


def decode_schema(payload: bytes) -> tuple[int, int]:
    crc32, count = _unpack_exact(SCHEMA_PAYLOAD, payload, "schema")
    return int(crc32), int(count)


def encode_subscribe(event_mask: int = EVENT_ALL) -> bytes:
    return SUBSCRIBE_PAYLOAD.pack(event_mask)


def decode_subscribe(payload: bytes) -> int:
    return int(_unpack_exact(SUBSCRIBE_PAYLOAD, payload, "subscribe")[0])


def encode_ack(result: int = 0) -> bytes:
    return ACK_PAYLOAD.pack(result)


def decode_ack(payload: bytes) -> int:
    return int(_unpack_exact(ACK_PAYLOAD, payload, "ack")[0])


def encode_error(code: int, detail: int = 0) -> bytes:
    return ERROR_PAYLOAD.pack(code, detail)


def decode_error(payload: bytes) -> tuple[int, int]:
    code, detail = _unpack_exact(ERROR_PAYLOAD, payload, "error")
    return int(code), int(detail)


def encode_keepalive(timestamp_us: int) -> bytes:
    return KEEPALIVE_PAYLOAD.pack(timestamp_us)


def decode_keepalive(payload: bytes) -> int:
    return int(_unpack_exact(KEEPALIVE_PAYLOAD, payload, "keepalive")[0])
