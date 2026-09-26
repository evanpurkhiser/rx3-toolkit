from pathlib import Path
import tempfile
import unittest
import wave

from tools.rx3_audio.protocol import (
    CONFIG,
    PCM,
    Decoder,
    Message,
    ProtocolError,
    S16_LE,
    StreamConfig,
)
from tools.rx3_audio.recorder import MAX_WAV_DATA_BYTES, StreamRecorder, WavWriter


class AudioProtocolTest(unittest.TestCase):
    def test_chunked_round_trip(self) -> None:
        encoded = Message(PCM, 0, 7, 1024, 3, b"\x01\x02\x03\x04").encode()
        decoder = Decoder()

        messages = []
        for byte in encoded:
            messages.extend(decoder.feed(bytes((byte,))))

        self.assertEqual(messages, [Message(PCM, 0, 7, 1024, 3, b"\x01\x02\x03\x04")])

    def test_config_round_trip(self) -> None:
        config = StreamConfig(44_100, 2, S16_LE, 441)
        self.assertEqual(StreamConfig.decode(config.encode()), config)

    def test_rejects_unsupported_format(self) -> None:
        with self.assertRaisesRegex(ProtocolError, "unsupported sample format"):
            StreamConfig(44_100, 2, 99, 441).encode()


class WavWriterTest(unittest.TestCase):
    def test_file_is_valid_before_close(self) -> None:
        config = StreamConfig(44_100, 2, S16_LE, 2)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.wav"
            writer = WavWriter(path, config)
            writer.write(b"\x01\x00\x02\x00\x03\x00\x04\x00")
            writer.flush()

            with wave.open(str(path), "rb") as recording:
                self.assertEqual(recording.getparams()[:4], (2, 2, 44_100, 2))
                self.assertEqual(recording.readframes(2), b"\x01\x00\x02\x00\x03\x00\x04\x00")
            writer.close()

    def test_rejects_partial_sample_frame(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            writer = WavWriter(
                Path(directory) / "capture.wav",
                StreamConfig(44_100, 2, S16_LE, 2),
            )
            with self.assertRaisesRegex(ProtocolError, "complete sample frame"):
                writer.write(b"\x00")
            writer.close()

    def test_rejects_data_beyond_riff_limit_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            writer = WavWriter(
                Path(directory) / "capture.wav",
                StreamConfig(44_100, 2, S16_LE, 2),
            )
            writer.data_bytes = MAX_WAV_DATA_BYTES - 3
            with self.assertRaisesRegex(OverflowError, "4 GiB RIFF/WAV limit"):
                writer.write(b"\x00" * 4)
            writer.data_bytes = 0
            writer.close()


class StreamRecorderTest(unittest.TestCase):
    def test_rejects_pcm_larger_than_configured_maximum(self) -> None:
        config = StreamConfig(44_100, 2, S16_LE, 2)
        with tempfile.TemporaryDirectory() as directory:
            recorder = StreamRecorder(Path(directory) / "capture.wav")
            recorder.begin_connection()
            recorder.handle(Message(CONFIG, 0, 0, 0, 0, config.encode()))
            with self.assertRaisesRegex(ProtocolError, "configured maximum"):
                recorder.handle(Message(PCM, 0, 1, 0, 0, b"\x00" * 12))
            recorder.close()

    def test_tracks_gaps_and_reconnects(self) -> None:
        config = StreamConfig(44_100, 2, S16_LE, 2)
        with tempfile.TemporaryDirectory() as directory:
            recorder = StreamRecorder(Path(directory) / "capture.wav", stats_interval=60)
            recorder.begin_connection()
            recorder.handle(Message(CONFIG, 0, 10, 100, 5, config.encode()))
            recorder.handle(Message(PCM, 0, 11, 100, 5, b"\x00" * 8))
            recorder.handle(Message(PCM, 0, 13, 105, 8, b"\x00" * 8))
            recorder.begin_connection()
            recorder.handle(Message(CONFIG, 0, 0, 0, 0, config.encode()))
            recorder.handle(Message(PCM, 0, 1, 0, 0, b"\x00" * 8))
            recorder.close()

            self.assertEqual(recorder.stats.connections, 2)
            self.assertEqual(recorder.stats.sequence_gaps, 1)
            self.assertEqual(recorder.stats.missing_frames, 3)
            self.assertEqual(recorder.stats.discontinuities, 1)
            self.assertEqual(recorder.stats.sender_dropped_frames, 3)
            self.assertEqual(recorder.stats.blocks, 3)

    def test_reconnect_reports_audio_missing_while_socket_was_absent(self) -> None:
        config = StreamConfig(44_100, 2, S16_LE, 2)
        with tempfile.TemporaryDirectory() as directory:
            recorder = StreamRecorder(Path(directory) / "capture.wav", stats_interval=60)
            recorder.begin_connection()
            recorder.handle(Message(CONFIG, 0, 0, 100, 0, config.encode()))
            recorder.handle(Message(PCM, 0, 1, 100, 0, b"\x00" * 8))

            recorder.begin_connection()
            recorder.handle(Message(CONFIG, 0, 0, 150, 0, config.encode()))
            recorder.handle(Message(PCM, 0, 1, 150, 0, b"\x00" * 8))
            recorder.close()

            self.assertEqual(recorder.stats.connections, 2)
            self.assertEqual(recorder.stats.missing_frames, 48)
            self.assertEqual(recorder.stats.discontinuities, 0)


if __name__ == "__main__":
    unittest.main()
