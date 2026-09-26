"""RX3 framebuffer stream protocol and host-side frame reconstruction."""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Iterator
import zlib


MAGIC = b"RX3F"
VERSION = 1

HELLO = 1
FRAME = 2
HEARTBEAT = 3

PIXEL_RGB565_LE = 1
FLAG_KEYFRAME = 1 << 0

HEADER = struct.Struct("!4sBBHII")
HELLO_PAYLOAD = struct.Struct("!HHHBB")
FRAME_PREFIX = struct.Struct("!QHH")
RECT_PREFIX = struct.Struct("!HHHHI")

RECT_CODEC_MASK = 0xC0000000
RECT_LENGTH_MASK = 0x3FFFFFFF
RECT_CODEC_RAW = 0x00000000
RECT_CODEC_XOR_RLE = 0x40000000
RECT_CODEC_XOR_RLE_LZ4 = 0x80000000
RECT_CODEC_XOR_RLE_ZLIB = 0xC0000000

MAX_PAYLOAD = 16 * 1024 * 1024
MAX_RECTS = 4096


class ProtocolError(ValueError):
    """The peer sent a malformed or unsupported stream."""


def decompress_lz4_block(source: bytes | memoryview, maximum: int) -> bytes:
    """Decode a raw LZ4 block while bounding its untrusted output."""
    data = memoryview(source)
    output = bytearray()
    offset = 0

    def extended_length(length: int) -> int:
        nonlocal offset
        if length != 15:
            return length
        while True:
            if offset >= len(data):
                raise ProtocolError("truncated LZ4 length")
            extension = data[offset]
            offset += 1
            length += extension
            if extension != 255:
                return length

    while offset < len(data):
        token = data[offset]
        offset += 1
        literal_length = extended_length(token >> 4)
        if offset + literal_length > len(data):
            raise ProtocolError("truncated LZ4 literals")
        if len(output) + literal_length > maximum:
            raise ProtocolError("LZ4 block exceeds rectangle limit")
        output.extend(data[offset:offset + literal_length])
        offset += literal_length
        if offset == len(data):
            return bytes(output)
        if offset + 2 > len(data):
            raise ProtocolError("truncated LZ4 match offset")
        match_offset = data[offset] | data[offset + 1] << 8
        offset += 2
        if not match_offset or match_offset > len(output):
            raise ProtocolError("invalid LZ4 match offset")
        match_length = extended_length(token & 0x0F) + 4
        if len(output) + match_length > maximum:
            raise ProtocolError("LZ4 block exceeds rectangle limit")
        for _ in range(match_length):
            output.append(output[-match_offset])

    raise ProtocolError("LZ4 block has no final literal sequence")


@dataclass(frozen=True)
class Message:
    type: int
    flags: int
    sequence: int
    payload: bytes

    @property
    def is_keyframe(self) -> bool:
        return bool(self.flags & FLAG_KEYFRAME)


@dataclass(frozen=True)
class StreamInfo:
    width: int
    height: int
    stride: int
    pixel_format: int
    tile_size: int

    @property
    def frame_size(self) -> int:
        return self.stride * self.height


@dataclass(frozen=True)
class FrameUpdate:
    sequence: int
    timestamp_ns: int
    rectangles: int
    payload_bytes: int


class Decoder:
    """Decode arbitrarily chunked TCP input into complete RX3F messages."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> Iterator[Message]:
        self._buffer.extend(data)

        while len(self._buffer) >= HEADER.size:
            magic, version, message_type, flags, sequence, length = HEADER.unpack_from(
                self._buffer
            )
            if magic != MAGIC:
                raise ProtocolError("message does not start with RX3F")
            if version != VERSION:
                raise ProtocolError(f"unsupported RX3F version {version}")
            if length > MAX_PAYLOAD:
                raise ProtocolError(f"payload is too large: {length} bytes")

            message_size = HEADER.size + length
            if len(self._buffer) < message_size:
                return

            payload = bytes(self._buffer[HEADER.size:message_size])
            del self._buffer[:message_size]
            yield Message(message_type, flags, sequence, payload)


def decode_hello(payload: bytes) -> StreamInfo:
    if len(payload) != HELLO_PAYLOAD.size:
        raise ProtocolError(f"HELLO payload is {len(payload)} bytes")

    info = StreamInfo(*HELLO_PAYLOAD.unpack(payload))
    if not info.width or not info.height:
        raise ProtocolError("frame geometry cannot be empty")
    if info.stride < info.width * 2:
        raise ProtocolError("frame stride is shorter than one RGB565 row")
    if info.pixel_format != PIXEL_RGB565_LE:
        raise ProtocolError(f"unsupported pixel format {info.pixel_format}")
    if not info.tile_size:
        raise ProtocolError("tile size cannot be zero")
    if info.frame_size > MAX_PAYLOAD:
        raise ProtocolError(f"frame is too large: {info.frame_size} bytes")
    return info


class Framebuffer:
    """Apply dirty rectangles and retain the latest complete RGB565 frame."""

    def __init__(self, info: StreamInfo) -> None:
        self.info = info
        self.pixels = bytearray(info.frame_size)
        self.last_sequence: int | None = None

    def apply(self, message: Message) -> FrameUpdate:
        if message.type != FRAME:
            raise ProtocolError("expected a FRAME message")
        if len(message.payload) < FRAME_PREFIX.size:
            raise ProtocolError("FRAME payload has no prefix")

        if message.is_keyframe:
            self.pixels[:] = bytes(len(self.pixels))

        timestamp_ns, rectangle_count, _reserved = FRAME_PREFIX.unpack_from(
            message.payload
        )
        if rectangle_count > MAX_RECTS:
            raise ProtocolError(f"too many rectangles: {rectangle_count}")

        offset = FRAME_PREFIX.size
        payload_bytes = 0
        for _ in range(rectangle_count):
            if offset + RECT_PREFIX.size > len(message.payload):
                raise ProtocolError("truncated rectangle header")
            x, y, width, height, encoded_length = RECT_PREFIX.unpack_from(
                message.payload, offset
            )
            offset += RECT_PREFIX.size

            expected_length = width * height * 2
            if not width or not height:
                raise ProtocolError("rectangle geometry cannot be empty")
            if x + width > self.info.width or y + height > self.info.height:
                raise ProtocolError("rectangle extends beyond the framebuffer")
            codec = encoded_length & RECT_CODEC_MASK
            data_length = encoded_length & RECT_LENGTH_MASK
            if codec == RECT_CODEC_RAW and data_length != expected_length:
                raise ProtocolError(
                    f"raw rectangle contains {data_length} bytes, expected {expected_length}"
                )
            if codec not in (
                RECT_CODEC_RAW, RECT_CODEC_XOR_RLE, RECT_CODEC_XOR_RLE_LZ4,
                RECT_CODEC_XOR_RLE_ZLIB,
            ):
                raise ProtocolError(f"unsupported rectangle codec {codec >> 30}")
            if offset + data_length > len(message.payload):
                raise ProtocolError("truncated rectangle pixels")

            source = memoryview(message.payload)[offset:offset + data_length]
            if codec == RECT_CODEC_RAW:
                self._apply_raw(source, x, y, width, height)
            elif codec == RECT_CODEC_XOR_RLE:
                self._apply_xor_rle(source, x, y, width, height)
            elif codec == RECT_CODEC_XOR_RLE_LZ4:
                inflated = decompress_lz4_block(source, expected_length)
                self._apply_xor_rle(memoryview(inflated), x, y, width, height)
            else:
                try:
                    inflated = zlib.decompress(source)
                except zlib.error as error:
                    raise ProtocolError(f"invalid zlib rectangle: {error}") from error
                if len(inflated) > expected_length:
                    raise ProtocolError("inflated XOR-RLE rectangle is too large")
                self._apply_xor_rle(memoryview(inflated), x, y, width, height)
            offset += data_length
            payload_bytes += data_length

        if offset != len(message.payload):
            raise ProtocolError(f"FRAME has {len(message.payload) - offset} trailing bytes")

        self.last_sequence = message.sequence
        return FrameUpdate(
            sequence=message.sequence,
            timestamp_ns=timestamp_ns,
            rectangles=rectangle_count,
            payload_bytes=payload_bytes,
        )

    def _apply_raw(
        self, source: memoryview, x: int, y: int, width: int, height: int
    ) -> None:
        row_bytes = width * 2
        for row in range(height):
            destination_offset = (y + row) * self.info.stride + x * 2
            source_offset = row * row_bytes
            self.pixels[destination_offset:destination_offset + row_bytes] = (
                source[source_offset:source_offset + row_bytes]
            )

    def _apply_xor_rle(
        self, source: memoryview, x: int, y: int, width: int, height: int
    ) -> None:
        pixel = 0
        source_offset = 0
        pixel_count = width * height
        while source_offset < len(source):
            control = source[source_offset]
            source_offset += 1
            run = (control & 0x7F) + 1
            if pixel + run > pixel_count:
                raise ProtocolError("XOR-RLE run extends beyond rectangle")
            if control & 0x80:
                literal_bytes = run * 2
                if source_offset + literal_bytes > len(source):
                    raise ProtocolError("truncated XOR-RLE literal run")
                for _ in range(run):
                    row, column = divmod(pixel, width)
                    destination = (y + row) * self.info.stride + (x + column) * 2
                    delta = source[source_offset] | source[source_offset + 1] << 8
                    current = self.pixels[destination] | self.pixels[destination + 1] << 8
                    value = current ^ delta
                    self.pixels[destination] = value & 0xFF
                    self.pixels[destination + 1] = value >> 8
                    source_offset += 2
                    pixel += 1
            else:
                pixel += run
        if pixel != pixel_count:
            raise ProtocolError(
                f"XOR-RLE rectangle covers {pixel} pixels, expected {pixel_count}"
            )


def encode_message(message_type: int, sequence: int, payload: bytes, flags: int = 0) -> bytes:
    """Encode protocol messages for tests and stream producers."""
    return HEADER.pack(MAGIC, VERSION, message_type, flags, sequence, len(payload)) + payload
