#!/usr/bin/env python3
"""Compare the LAN and USB views of a relayed Link Export handshake."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from pcap_link import LINK_MAGIC, link_packets


REQUEST_RESPONSES = {0x10: 0x11, 0x30: 0x31, 0x46: 0x47}


def packet_type(payload: bytes) -> int | None:
    if not payload.startswith(LINK_MAGIC) or len(payload) <= 10:
        return None
    return payload[10]


def device_name(payload: bytes) -> str | None:
    kind = packet_type(payload)
    if kind not in {0x11, 0x31, 0x47} or len(payload) <= 40:
        return None

    raw = payload[40:]
    if len(raw) % 2:
        raw = raw[:-1]
    return raw.decode("utf-16-be", errors="replace").rstrip("\0") or None


def load(path: Path) -> list[dict[str, object]]:
    events = []
    for packet in link_packets(path):
        kind = packet_type(packet.payload)
        if kind is None:
            continue
        events.append(
            {
                "timestamp": packet.timestamp,
                "source": packet.source_ip,
                "destination": packet.destination_ip,
                "source_port": packet.source_port,
                "destination_port": packet.destination_port,
                "type": kind,
                "length": len(packet.payload),
                "name": device_name(packet.payload),
                "payload": packet.payload,
            }
        )
    return events


def type_counts(events: list[dict[str, object]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for event in events:
        key = f"0x{event['type']:02x}"
        counts[key] = counts.get(key, 0) + 1
    return counts


def first_after(
    events: list[dict[str, object]], kind: int, timestamp: float
) -> dict[str, object] | None:
    return next(
        (
            event
            for event in events
            if event["type"] == kind and event["timestamp"] >= timestamp
        ),
        None,
    )


def public_event(event: dict[str, object] | None) -> dict[str, object] | None:
    if event is None:
        return None
    return {key: value for key, value in event.items() if key != "payload"}


def compare(lan_path: Path, usb_path: Path) -> dict[str, object]:
    lan_events = load(lan_path)
    usb_events = load(usb_path)
    exchanges = []

    for request_type, response_type in REQUEST_RESPONSES.items():
        requests = [event for event in usb_events if event["type"] == request_type]
        first_request = requests[0] if requests else None
        lan_response = (
            first_after(lan_events, response_type, first_request["timestamp"])
            if first_request
            else None
        )
        usb_response = (
            first_after(usb_events, response_type, first_request["timestamp"])
            if first_request
            else None
        )
        exchanges.append(
            {
                "request_type": f"0x{request_type:02x}",
                "response_type": f"0x{response_type:02x}",
                "usb_request_count": len(requests),
                "first_usb_request": public_event(first_request),
                "first_lan_response": public_event(lan_response),
                "first_usb_response": public_event(usb_response),
            }
        )

    return {
        "lan": str(lan_path),
        "usb": str(usb_path),
        "lan_type_counts": type_counts(lan_events),
        "usb_type_counts": type_counts(usb_events),
        "exchanges": exchanges,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("lan", type=Path)
    parser.add_argument("usb", type=Path)
    args = parser.parse_args()
    print(json.dumps(compare(args.lan, args.usb), indent=2))


if __name__ == "__main__":
    main()
