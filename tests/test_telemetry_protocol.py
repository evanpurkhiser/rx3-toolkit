from __future__ import annotations

import unittest

from tools.rx3_telemetry.protocol import (
    Decoder,
    encode_hello,
    encode_metadata,
    encode_state,
)


class TelemetryProtocolTests(unittest.TestCase):
    def test_hello_identifies_verified_build(self) -> None:
        event = Decoder().feed(encode_hello())[0]
        self.assertEqual(event["type"], "hello")
        self.assertEqual(event["deckCount"], 2)
        self.assertEqual(event["rbpBuildPrefix"], "cf309238")

    def test_state_maps_to_prolink_shape(self) -> None:
        report = encode_state(
            2, sequence=9, loaded=True, on_air=True, play_state=1,
            generation=4, track_number=123, bpm_x100=12750, tempo_raw=-80,
        )
        event = Decoder().feed(report)[0]
        self.assertEqual(event["playback"], "mode-1")
        self.assertEqual(event["bpm"], 127.5)
        self.assertEqual(event["prolinkState"]["playState"], 0x05)
        self.assertTrue(event["prolinkState"]["isOnAir"])

    def test_metadata_reassembles_out_of_order(self) -> None:
        reports = encode_metadata(
            1, sequence=10, field=1, generation=2,
            value="A title longer than eight bytes",
        )
        decoder = Decoder()
        events = []
        for report in reversed(reports):
            events.extend(decoder.feed(report))
        self.assertEqual(len(events), 1)
        self.assertEqual(events[0]["field"], "title")
        self.assertEqual(events[0]["value"], "A title longer than eight bytes")

    def test_optional_hid_report_id_is_accepted(self) -> None:
        event = Decoder().feed(b"\0" + encode_hello())[0]
        self.assertEqual(event["type"], "hello")


if __name__ == "__main__":
    unittest.main()
