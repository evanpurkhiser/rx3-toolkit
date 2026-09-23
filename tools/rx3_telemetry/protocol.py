"""RX3T protocol v1 decoder and simulator helpers."""

from __future__ import annotations

from dataclasses import dataclass, field
import struct
from typing import Any


MAGIC = b"RX3T"
VERSION = 1
REPORT_SIZE = 20
HELLO = 1
STATE = 2
METADATA = 3
FIELD_NAMES = {1: "title", 2: "artist", 3: "album", 4: "key"}


def _envelope(message_type: int, deck: int, sequence: int, payload: bytes) -> bytes:
    if len(payload) != 12:
        raise ValueError("RX3T payloads are exactly 12 bytes")
    return MAGIC + bytes((VERSION, message_type, deck, sequence & 0xFF)) + payload


def encode_hello(sequence: int = 0) -> bytes:
    payload = bytes((2, 0x03, 0, 0, 0xCF, 0x30, 0x92, 0x38, 0, 0, 0, 0))
    return _envelope(HELLO, 0, sequence, payload)


def encode_state(
    deck: int,
    *,
    sequence: int,
    loaded: bool,
    on_air: bool,
    play_state: int,
    generation: int,
    track_number: int,
    bpm_x100: int,
    tempo_raw: int,
) -> bytes:
    flags = int(loaded) | (int(on_air) << 1)
    payload = bytes((flags, play_state, generation & 0xFF, 0))
    payload += struct.pack("<IHh", track_number, bpm_x100, tempo_raw)
    return _envelope(STATE, deck, sequence, payload)


def encode_metadata(
    deck: int,
    *,
    sequence: int,
    field: int,
    generation: int,
    value: str,
) -> list[bytes]:
    encoded = value.encode("utf-8")
    chunks = [encoded[offset : offset + 8] for offset in range(0, len(encoded), 8)]
    chunks = chunks or [b""]
    return [
        _envelope(
            METADATA,
            deck,
            sequence + index,
            bytes((field, generation & 0xFF, index, len(chunks)))
            + chunk.ljust(8, b"\0"),
        )
        for index, chunk in enumerate(chunks)
    ]


def _playback_name(raw: int, loaded: bool) -> str:
    if not loaded:
        return "empty"
    return {1: "playing", 2: "cued", 3: "paused"}.get(raw, "unknown")


def _prolink_play_state(raw: int, loaded: bool) -> int:
    if not loaded:
        return 0x00
    return {1: 0x03, 2: 0x06, 3: 0x05}.get(raw, 0x05)


@dataclass
class Decoder:
    fragments: dict[tuple[int, int, int], dict[int, bytes]] = field(
        default_factory=dict
    )
    fragment_counts: dict[tuple[int, int, int], int] = field(default_factory=dict)
    packet_number: int = 0

    def feed(self, report: bytes | bytearray | list[int]) -> list[dict[str, Any]]:
        data = bytes(report)
        if len(data) == REPORT_SIZE + 1 and data[0] == 0:
            data = data[1:]
        if len(data) != REPORT_SIZE:
            raise ValueError(f"expected {REPORT_SIZE} bytes, received {len(data)}")
        if data[:4] != MAGIC:
            raise ValueError("report does not start with RX3T")
        if data[4] != VERSION:
            raise ValueError(f"unsupported RX3T version {data[4]}")

        message_type, deck, sequence = data[5], data[6], data[7]
        payload = data[8:]
        if message_type == HELLO:
            return [{
                "type": "hello",
                "version": data[4],
                "sequence": sequence,
                "deckCount": payload[0],
                "capabilities": payload[1],
                "rbpBuildPrefix": payload[4:8].hex(),
            }]
        if message_type == STATE:
            return [self._decode_state(deck, sequence, payload)]
        if message_type == METADATA:
            event = self._decode_metadata(deck, sequence, payload)
            return [event] if event else []
        return [{
            "type": "unknown",
            "messageType": message_type,
            "deck": deck,
            "sequence": sequence,
            "payload": payload.hex(),
        }]

    def _decode_state(self, deck: int, sequence: int, payload: bytes) -> dict[str, Any]:
        flags, raw_play_state, generation = payload[:3]
        track_number, bpm_x100, tempo_raw = struct.unpack("<IHh", payload[4:12])
        loaded = bool(flags & 0x01)
        on_air = bool(flags & 0x02)
        self.packet_number += 1
        return {
            "type": "state",
            "deck": deck,
            "sequence": sequence,
            "generation": generation,
            "loaded": loaded,
            "onAir": on_air,
            "rawPlayState": raw_play_state,
            "playback": _playback_name(raw_play_state, loaded),
            "trackNumber": track_number,
            "bpm": bpm_x100 / 100 if loaded and bpm_x100 else None,
            "tempoRaw": tempo_raw,
            "prolinkState": {
                "deviceId": deck,
                "trackId": track_number if loaded else 0,
                "trackDeviceId": deck,
                "trackSlot": 0x03,
                "trackType": 0x01,
                "playState": _prolink_play_state(raw_play_state, loaded),
                "isOnAir": on_air,
                "isSync": False,
                "isMaster": False,
                "isEmergencyMode": False,
                "trackBPM": bpm_x100 / 100 if loaded and bpm_x100 else None,
                "effectivePitch": tempo_raw / 100,
                "sliderPitch": tempo_raw / 100,
                "beatInMeasure": 0,
                "beatsUntilCue": None,
                "beat": None,
                "packetNum": self.packet_number,
            },
        }

    def _decode_metadata(
        self, deck: int, sequence: int, payload: bytes
    ) -> dict[str, Any] | None:
        field_id, generation, index, count = payload[:4]
        if not count or index >= count:
            raise ValueError("invalid metadata fragment coordinates")
        key = (deck, generation, field_id)
        self.fragments.setdefault(key, {})[index] = payload[4:12]
        self.fragment_counts[key] = count
        fragments = self.fragments[key]
        if len(fragments) != count or any(part not in fragments for part in range(count)):
            return None

        value = b"".join(fragments[part] for part in range(count)).rstrip(b"\0")
        del self.fragments[key]
        del self.fragment_counts[key]
        return {
            "type": "metadata",
            "deck": deck,
            "sequence": sequence,
            "generation": generation,
            "field": FIELD_NAMES.get(field_id, f"field-{field_id}"),
            "value": value.decode("utf-8", errors="replace"),
        }
