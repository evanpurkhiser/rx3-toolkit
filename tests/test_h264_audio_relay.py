from __future__ import annotations

import unittest

from tools.rx3_audio.protocol import CONFIG, PCM, Message, StreamConfig
from tools.rx3_h264.relay import AudioHub, AudioSubscription


def config_message(sequence: int = 0) -> Message:
    config = StreamConfig(44_100, 2, 1, 64)
    return Message(CONFIG, 0, sequence, 0, 0, config.encode())


def pcm_message(sequence: int, value: int = 0) -> Message:
    return Message(PCM, 0, sequence, sequence * 64, 0, bytes([value]) * 256)


class AudioSubscriptionTest(unittest.TestCase):
    def test_batches_config_and_pcm(self) -> None:
        subscription = AudioSubscription()
        config = config_message().encode()
        pcm = pcm_message(1).encode()

        subscription.put(config_message())
        subscription.put(pcm_message(1))

        self.assertEqual(subscription.get_batch(0), config + pcm)

    def test_overflow_discards_stale_audio_and_repeats_config(self) -> None:
        subscription = AudioSubscription(maximum=3)
        config = config_message().encode()
        newest = pcm_message(3, 3).encode() + pcm_message(4, 4).encode()
        subscription.put(config_message())
        subscription.put(pcm_message(1, 1))
        subscription.put(pcm_message(2, 2))
        subscription.put(pcm_message(3, 3))
        subscription.put(pcm_message(4, 4))

        self.assertEqual(subscription.get_batch(0), config + newest)
        self.assertEqual(subscription.dropped, 3)


class AudioHubTest(unittest.TestCase):
    def test_new_subscriber_receives_latest_config(self) -> None:
        hub = AudioHub()
        message = config_message()
        hub.publish(message)

        subscription = hub.subscribe()

        self.assertEqual(subscription.get_batch(0), message.encode())

    def test_health_reports_stream_and_client_state(self) -> None:
        hub = AudioHub()
        hub.set_connected(True)
        hub.publish(config_message())
        subscription = hub.subscribe()

        health = hub.health()

        self.assertTrue(health["ok"])
        self.assertTrue(health["upstream_connected"])
        self.assertEqual(health["clients"], 1)
        self.assertEqual(health["blocks"], 0)
        self.assertEqual(health["errors"], 0)
        hub.unsubscribe(subscription)


if __name__ == "__main__":
    unittest.main()
