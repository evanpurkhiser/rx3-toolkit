#!/usr/bin/env python3
"""Summarize Link Export traffic from classic Ethernet PCAP files."""

from __future__ import annotations

import argparse
import collections
import ipaddress
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator


LINK_PORTS = {111, 2049, 12523, 50000, 50001, 50002, 50111}
LINK_MAGIC = b"Qspt1WmJOL"


@dataclass(frozen=True)
class Packet:
    timestamp: float
    source_mac: str
    destination_mac: str
    source_ip: str
    destination_ip: str
    protocol: str
    source_port: int | None
    destination_port: int | None
    payload: bytes
    frame_length: int

    @property
    def flow(self) -> tuple[str, str, int | None, str, int | None, int]:
        return (
            self.protocol,
            self.source_ip,
            self.source_port,
            self.destination_ip,
            self.destination_port,
            self.frame_length,
        )


def format_mac(data: bytes) -> str:
    return ":".join(f"{octet:02x}" for octet in data)


def read_pcap(stream: BinaryIO) -> Iterator[tuple[float, bytes, int]]:
    header = stream.read(24)
    if len(header) != 24:
        raise ValueError("truncated PCAP header")

    magic = header[:4]
    formats = {
        b"\xd4\xc3\xb2\xa1": ("<", 1_000_000),
        b"\xa1\xb2\xc3\xd4": (">", 1_000_000),
        b"\x4d\x3c\xb2\xa1": ("<", 1_000_000_000),
        b"\xa1\xb2\x3c\x4d": (">", 1_000_000_000),
    }
    if magic not in formats:
        raise ValueError("only classic PCAP input is supported")

    byte_order, timestamp_scale = formats[magic]
    link_type = struct.unpack_from(f"{byte_order}I", header, 20)[0]
    if link_type != 1:
        raise ValueError(f"only Ethernet PCAP is supported, got link type {link_type}")

    packet_header = struct.Struct(f"{byte_order}IIII")
    while raw_header := stream.read(packet_header.size):
        if len(raw_header) != packet_header.size:
            raise ValueError("truncated packet header")

        seconds, fraction, captured_length, original_length = packet_header.unpack(raw_header)
        frame = stream.read(captured_length)
        if len(frame) != captured_length:
            raise ValueError("truncated packet body")

        yield seconds + fraction / timestamp_scale, frame, original_length


def parse_ethernet(timestamp: float, frame: bytes, original_length: int) -> Packet | None:
    if len(frame) < 14:
        return None

    destination_mac = format_mac(frame[:6])
    source_mac = format_mac(frame[6:12])
    ether_type = struct.unpack_from("!H", frame, 12)[0]
    offset = 14

    while ether_type in {0x8100, 0x88A8}:
        if len(frame) < offset + 4:
            return None
        ether_type = struct.unpack_from("!H", frame, offset + 2)[0]
        offset += 4

    if ether_type != 0x0800 or len(frame) < offset + 20:
        return None

    version_ihl = frame[offset]
    if version_ihl >> 4 != 4:
        return None

    ip_header_length = (version_ihl & 0x0F) * 4
    if ip_header_length < 20 or len(frame) < offset + ip_header_length:
        return None

    protocol_number = frame[offset + 9]
    source_ip = str(ipaddress.IPv4Address(frame[offset + 12 : offset + 16]))
    destination_ip = str(ipaddress.IPv4Address(frame[offset + 16 : offset + 20]))
    transport_offset = offset + ip_header_length

    if protocol_number == 17 and len(frame) >= transport_offset + 8:
        source_port, destination_port, udp_length = struct.unpack_from(
            "!HHH", frame, transport_offset
        )
        payload_end = min(len(frame), transport_offset + udp_length)
        payload = frame[transport_offset + 8 : payload_end]
        protocol = "udp"
    elif protocol_number == 6 and len(frame) >= transport_offset + 20:
        source_port, destination_port = struct.unpack_from("!HH", frame, transport_offset)
        tcp_header_length = (frame[transport_offset + 12] >> 4) * 4
        if tcp_header_length < 20 or len(frame) < transport_offset + tcp_header_length:
            return None
        payload = frame[transport_offset + tcp_header_length :]
        protocol = "tcp"
    else:
        return None

    return Packet(
        timestamp=timestamp,
        source_mac=source_mac,
        destination_mac=destination_mac,
        source_ip=source_ip,
        destination_ip=destination_ip,
        protocol=protocol,
        source_port=source_port,
        destination_port=destination_port,
        payload=payload,
        frame_length=original_length,
    )


def link_packets(path: Path) -> Iterator[Packet]:
    with path.open("rb") as stream:
        for timestamp, frame, original_length in read_pcap(stream):
            packet = parse_ethernet(timestamp, frame, original_length)
            if packet is None:
                continue
            if packet.source_port not in LINK_PORTS and packet.destination_port not in LINK_PORTS:
                continue
            yield packet


def summarize(path: Path) -> dict[str, object]:
    flows: dict[tuple[object, ...], dict[str, object]] = {}
    first_timestamp = None
    last_timestamp = None
    total_packets = 0

    for packet in link_packets(path):
        total_packets += 1
        first_timestamp = packet.timestamp if first_timestamp is None else first_timestamp
        last_timestamp = packet.timestamp
        row = flows.setdefault(
            packet.flow,
            {
                "packets": 0,
                "bytes": 0,
                "first": packet.timestamp,
                "last": packet.timestamp,
                "lengths": collections.Counter(),
                "payload_prefix": packet.payload[:48].hex(),
                "source_mac": packet.source_mac,
                "destination_mac": packet.destination_mac,
            },
        )
        row["packets"] += 1
        row["bytes"] += packet.frame_length
        row["last"] = packet.timestamp
        row["lengths"][len(packet.payload)] += 1

    serialized_flows = []
    for flow, row in sorted(flows.items(), key=lambda item: (-item[1]["packets"], item[0])):
        protocol, source_ip, source_port, destination_ip, destination_port, frame_length = flow
        serialized_flows.append(
            {
                "protocol": protocol,
                "source": f"{source_ip}:{source_port}",
                "destination": f"{destination_ip}:{destination_port}",
                "frame_length": frame_length,
                **{key: value for key, value in row.items() if key != "lengths"},
                "payload_lengths": dict(sorted(row["lengths"].items())),
            }
        )

    return {
        "file": str(path),
        "packets": total_packets,
        "first": first_timestamp,
        "last": last_timestamp,
        "duration": None
        if first_timestamp is None or last_timestamp is None
        else last_timestamp - first_timestamp,
        "flows": serialized_flows,
    }


def event_rows(path: Path) -> Iterator[dict[str, object]]:
    first_timestamp = None
    for index, packet in enumerate(link_packets(path), start=1):
        first_timestamp = packet.timestamp if first_timestamp is None else first_timestamp
        magic = packet.payload.startswith(LINK_MAGIC)
        yield {
            "index": index,
            "timestamp": packet.timestamp,
            "relative": packet.timestamp - first_timestamp,
            "protocol": packet.protocol,
            "source": f"{packet.source_ip}:{packet.source_port}",
            "destination": f"{packet.destination_ip}:{packet.destination_port}",
            "source_mac": packet.source_mac,
            "destination_mac": packet.destination_mac,
            "payload_length": len(packet.payload),
            "link_magic": magic,
            "link_type": packet.payload[10] if magic and len(packet.payload) > 10 else None,
            "payload": packet.payload.hex(),
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("summary", "events"))
    parser.add_argument("pcap", type=Path)
    parser.add_argument("--limit", type=int)
    args = parser.parse_args()

    if args.mode == "summary":
        print(json.dumps(summarize(args.pcap), indent=2))
        return

    for index, row in enumerate(event_rows(args.pcap)):
        if args.limit is not None and index >= args.limit:
            break
        print(json.dumps(row, separators=(",", ":")))


if __name__ == "__main__":
    main()
