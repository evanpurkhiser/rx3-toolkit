# SPDX-License-Identifier: MPL-2.0
from io import BytesIO
import unittest

from tools.rx3_h264.client import copy_stream
from tools.rx3_h264.protocol import (
    ACCESS_UNIT,
    CONFIG,
    FLAG_KEYFRAME,
    Decoder,
    ProtocolError,
    encode_message,
)


class ChunkedReceiver:
    def __init__(self, *chunks: bytes):
        self.chunks = iter(chunks)

    def recv(self, _size: int) -> bytes:
        return next(self.chunks, b"")


class H264ProtocolTest(unittest.TestCase):
    def test_decodes_fragmented_messages(self):
        encoded = encode_message(CONFIG, 7, 1234, b"\x00\x00\x00\x01g")
        decoder = Decoder()

        self.assertEqual(list(decoder.feed(encoded[:8])), [])
        self.assertEqual(list(decoder.feed(encoded[8:23])), [])
        [message] = decoder.feed(encoded[23:])

        self.assertEqual(message.type, CONFIG)
        self.assertEqual(message.sequence, 7)
        self.assertEqual(message.timestamp_us, 1234)
        self.assertEqual(message.payload, b"\x00\x00\x00\x01g")

    def test_decodes_multiple_messages(self):
        data = (
            encode_message(CONFIG, 1, 0, b"config")
            + encode_message(ACCESS_UNIT, 2, 33333, b"frame", FLAG_KEYFRAME)
        )

        messages = list(Decoder().feed(data))

        self.assertEqual([message.type for message in messages], [CONFIG, ACCESS_UNIT])
        self.assertTrue(messages[1].is_keyframe)

    def test_rejects_invalid_magic(self):
        encoded = bytearray(encode_message(CONFIG, 1, 0, b"config"))
        encoded[:4] = b"NOPE"

        with self.assertRaisesRegex(ProtocolError, "RX3H"):
            list(Decoder().feed(encoded))

    def test_client_writes_annex_b_stream(self):
        stream = (
            encode_message(CONFIG, 1, 0, b"config")
            + encode_message(ACCESS_UNIT, 2, 100, b"key", FLAG_KEYFRAME)
            + encode_message(ACCESS_UNIT, 3, 200, b"delta")
        )
        output = BytesIO()

        stats = copy_stream(
            ChunkedReceiver(stream[:17], stream[17:41], stream[41:]),
            output,
        )

        self.assertEqual(output.getvalue(), b"configkeydelta")
        self.assertEqual(stats.frames, 2)
        self.assertEqual(stats.keyframes, 1)
        self.assertEqual(stats.first_timestamp_us, 100)
        self.assertEqual(stats.last_timestamp_us, 200)

    def test_client_rejects_sequence_gaps(self):
        stream = (
            encode_message(CONFIG, 1, 0, b"config")
            + encode_message(ACCESS_UNIT, 3, 100, b"key", FLAG_KEYFRAME)
        )

        with self.assertRaisesRegex(ProtocolError, "sequence gap"):
            copy_stream(ChunkedReceiver(stream), BytesIO())

    def test_client_requires_configuration(self):
        stream = encode_message(ACCESS_UNIT, 1, 100, b"key", FLAG_KEYFRAME)

        with self.assertRaisesRegex(ProtocolError, "before SPS/PPS"):
            copy_stream(ChunkedReceiver(stream), BytesIO())


if __name__ == "__main__":
    unittest.main()
