import unittest

from tools.rx3_h264.protocol import (
    ACCESS_UNIT,
    CONFIG,
    FLAG_KEYFRAME,
    Decoder,
    ProtocolError,
    encode_message,
)
from tools.rx3_h264.relay import RelayHub, Subscription, _websocket_frame


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

    def test_subscription_drops_stale_gop_and_waits_for_keyframe(self):
        subscription = Subscription(maximum=2)
        decoder = Decoder()
        messages = list(decoder.feed(
            encode_message(ACCESS_UNIT, 1, 0, b"key", FLAG_KEYFRAME)
            + encode_message(ACCESS_UNIT, 2, 1, b"delta")
            + encode_message(ACCESS_UNIT, 3, 2, b"overflow")
            + encode_message(ACCESS_UNIT, 4, 3, b"ignored")
            + encode_message(ACCESS_UNIT, 5, 4, b"new-key", FLAG_KEYFRAME)
        ))
        for message in messages:
            subscription.put(message)

        self.assertEqual(subscription.get(0), messages[-1].encode())
        self.assertEqual(subscription.dropped, 4)

    def test_new_subscription_receives_latest_config(self):
        hub = RelayHub()
        decoder = Decoder()
        [config] = decoder.feed(encode_message(CONFIG, 1, 0, b"config"))
        hub.publish(config)

        subscription = hub.subscribe()

        self.assertIsNone(subscription.get(0))

        [keyframe] = decoder.feed(
            encode_message(ACCESS_UNIT, 2, 1, b"key", FLAG_KEYFRAME)
        )
        subscription.put(keyframe)
        self.assertEqual(subscription.get(0), config.encode())
        self.assertEqual(subscription.get(0), keyframe.encode())

    def test_websocket_encodes_large_binary_frame(self):
        frame = _websocket_frame(b"x" * 70000)

        self.assertEqual(frame[:2], b"\x82\x7f")
        self.assertEqual(int.from_bytes(frame[2:10], "big"), 70000)
        self.assertEqual(len(frame), 70010)


if __name__ == "__main__":
    unittest.main()
