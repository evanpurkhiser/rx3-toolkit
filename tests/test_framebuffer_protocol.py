from __future__ import annotations

import struct
import socket
import unittest
import zlib

from tools.rx3_framebuffer.protocol import (
    FRAME,
    HELLO,
    FRAME_PREFIX,
    HELLO_PAYLOAD,
    RECT_PREFIX,
    RECT_CODEC_XOR_RLE,
    RECT_CODEC_XOR_RLE_LZ4,
    RECT_CODEC_XOR_RLE_ZLIB,
    Decoder,
    FLAG_KEYFRAME,
    Framebuffer,
    ProtocolError,
    decode_hello,
    decompress_lz4_block,
    encode_message,
)
from tools.rx3_framebuffer.web import INDEX_HTML, ViewerHub


def read_exact(connection: socket.socket, length: int) -> bytes:
    result = bytearray()
    while len(result) < length:
        result.extend(connection.recv(length - len(result)))
    return bytes(result)


def read_websocket_payload(connection: socket.socket) -> bytes:
    first, length = read_exact(connection, 2)
    if length == 126:
        length = struct.unpack("!H", read_exact(connection, 2))[0]
    elif length == 127:
        length = struct.unpack("!Q", read_exact(connection, 8))[0]
    if first != 0x82:
        raise AssertionError(f"unexpected WebSocket opcode {first:#x}")
    return read_exact(connection, length)


class RecordingSocket:
    def __init__(self) -> None:
        self.data = bytearray()

    def settimeout(self, _timeout: float) -> None:
        pass

    def sendall(self, payload: bytes) -> None:
        self.data.extend(payload)

    def recv(self, length: int) -> bytes:
        payload = bytes(self.data[:length])
        del self.data[:length]
        return payload

    def close(self) -> None:
        pass


class ProtocolTests(unittest.TestCase):
    def test_web_viewer_decodes_lz4_frames_in_order(self) -> None:
        self.assertIn("function decompressLz4", INDEX_HTML)
        self.assertIn("codec === 2", INDEX_HTML)
        self.assertIn(
            "receiveQueue = receiveQueue.then(() => receive(event.data))",
            INDEX_HTML,
        )

    def test_decoder_accepts_fragmented_tcp_input(self) -> None:
        encoded = encode_message(HELLO, 7, HELLO_PAYLOAD.pack(4, 3, 8, 1, 2))
        decoder = Decoder()

        messages = []
        for byte in encoded:
            messages.extend(decoder.feed(bytes((byte,))))

        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0].sequence, 7)
        self.assertEqual(decode_hello(messages[0].payload).frame_size, 24)

    def test_framebuffer_applies_rectangle_with_destination_stride(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 3, 10, 1, 2))
        framebuffer = Framebuffer(info)
        pixels = bytes(range(8))
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(1, 1, 2, 2, len(pixels)) + pixels

        update = framebuffer.apply(
            next(Decoder().feed(encode_message(FRAME, 9, payload)))
        )

        self.assertEqual(update.sequence, 9)
        self.assertEqual(update.rectangles, 1)
        self.assertEqual(framebuffer.pixels[12:16], pixels[:4])
        self.assertEqual(framebuffer.pixels[22:26], pixels[4:])

    def test_keyframe_clears_pixels_outside_rectangles(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 3, 8, 1, 2))
        framebuffer = Framebuffer(info)
        framebuffer.pixels[:] = b"\xff" * len(framebuffer.pixels)
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(0, 0, 1, 1, 2) + b"\x12\x34"

        message = next(
            Decoder().feed(
                encode_message(FRAME, 9, payload, flags=FLAG_KEYFRAME)
            )
        )
        framebuffer.apply(message)

        self.assertEqual(framebuffer.pixels[:2], b"\x12\x34")
        self.assertEqual(framebuffer.pixels[2:], bytes(len(framebuffer.pixels) - 2))

    def test_framebuffer_applies_xor_rle_rectangle(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 2, 8, 1, 2))
        framebuffer = Framebuffer(info)
        framebuffer.pixels[:] = bytes.fromhex(
            "1111 2222 3333 4444 5555 6666 7777 8888"
        )
        # Skip one pixel, XOR two literals, then skip the remaining five.
        encoded = b"\x00\x81" + bytes.fromhex("ffff 0101") + b"\x04"
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(
            0, 0, 4, 2, RECT_CODEC_XOR_RLE | len(encoded)
        ) + encoded

        framebuffer.apply(next(Decoder().feed(encode_message(FRAME, 10, payload))))

        self.assertEqual(
            framebuffer.pixels,
            bytes.fromhex("1111 dddd 3232 4444 5555 6666 7777 8888"),
        )

    def test_framebuffer_applies_zlib_xor_rle_rectangle(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 2, 8, 1, 2))
        framebuffer = Framebuffer(info)
        framebuffer.pixels[:] = bytes.fromhex(
            "1111 2222 3333 4444 5555 6666 7777 8888"
        )
        encoded = zlib.compress(
            b"\x00\x81" + bytes.fromhex("ffff 0101") + b"\x04", 1
        )
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(
            0, 0, 4, 2, RECT_CODEC_XOR_RLE_ZLIB | len(encoded)
        ) + encoded

        framebuffer.apply(next(Decoder().feed(encode_message(FRAME, 11, payload))))

        self.assertEqual(
            framebuffer.pixels,
            bytes.fromhex("1111 dddd 3232 4444 5555 6666 7777 8888"),
        )

    def test_framebuffer_applies_lz4_xor_rle_rectangle(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 2, 8, 1, 2))
        framebuffer = Framebuffer(info)
        framebuffer.pixels[:] = bytes.fromhex(
            "1111 2222 3333 4444 5555 6666 7777 8888"
        )
        rle = b"\x00\x81" + bytes.fromhex("ffff 0101") + b"\x04"
        encoded = bytes((len(rle) << 4,)) + rle
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(
            0, 0, 4, 2, RECT_CODEC_XOR_RLE_LZ4 | len(encoded)
        ) + encoded

        framebuffer.apply(next(Decoder().feed(encode_message(FRAME, 12, payload))))

        self.assertEqual(
            framebuffer.pixels,
            bytes.fromhex("1111 dddd 3232 4444 5555 6666 7777 8888"),
        )

    def test_lz4_block_decodes_overlapping_match(self) -> None:
        encoded = b"\x44abcd\x04\x00\x10X"

        self.assertEqual(decompress_lz4_block(encoded, 13), b"abcdabcdabcdX")

    def test_lz4_block_rejects_malformed_input(self) -> None:
        malformed = (
            b"\xf0",                 # missing extended literal length
            b"\x10",                 # missing literal
            b"\x00\x00\x00",       # zero match offset
            b"\x10A\x02\x00",      # match before available output
            b"\x50abcde",            # exceeds output limit below
        )
        for encoded in malformed:
            with self.subTest(encoded=encoded), self.assertRaises(ProtocolError):
                decompress_lz4_block(encoded, 4)

    def test_framebuffer_rejects_invalid_rectangles(self) -> None:
        rectangles = [
            RECT_PREFIX.pack(3, 0, 2, 1, 4) + b"1234",
            RECT_PREFIX.pack(0, 0, 2, 1, 3) + b"123",
            RECT_PREFIX.pack(0, 0, 2, 2, 8) + b"1234",
        ]
        info = decode_hello(HELLO_PAYLOAD.pack(4, 3, 8, 1, 2))

        for rectangle in rectangles:
            with self.subTest(rectangle=rectangle), self.assertRaises(ProtocolError):
                payload = FRAME_PREFIX.pack(0, 1, 0) + rectangle
                Framebuffer(info).apply(
                    next(Decoder().feed(encode_message(FRAME, 1, payload)))
                )

    def test_decoder_rejects_unbounded_payload(self) -> None:
        malformed = struct.pack("!4sBBHII", b"RX3F", 1, FRAME, 0, 1, 0xFFFFFFFF)

        with self.assertRaisesRegex(ProtocolError, "too large"):
            list(Decoder().feed(malformed))

    def test_web_viewer_sends_snapshot_to_new_client(self) -> None:
        info = decode_hello(HELLO_PAYLOAD.pack(4, 3, 8, 1, 2))
        hub = ViewerHub()
        hub.configure(info)
        pixels = bytes(range(info.frame_size))
        payload = FRAME_PREFIX.pack(1234, 1, 0)
        payload += RECT_PREFIX.pack(0, 0, 4, 3, len(pixels)) + pixels
        message = next(Decoder().feed(encode_message(FRAME, 9, payload, 1)))
        hub.apply_and_publish(message)

        browser = RecordingSocket()
        try:
            hub.add(browser)
            hello = next(Decoder().feed(read_websocket_payload(browser)))
            snapshot = next(Decoder().feed(read_websocket_payload(browser)))

            self.assertEqual(hello.type, HELLO)
            self.assertTrue(snapshot.is_keyframe)
            restored = Framebuffer(info)
            restored.apply(snapshot)
            self.assertEqual(restored.pixels, pixels)
        finally:
            hub.remove(browser)
            browser.close()


if __name__ == "__main__":
    unittest.main()
