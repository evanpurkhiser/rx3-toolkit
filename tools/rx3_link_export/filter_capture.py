#!/usr/bin/env python3
"""Reduce a classic PCAP to one Pro DJ Link peer and discovery traffic."""

from __future__ import annotations

import argparse
import ipaddress
import mmap
import struct
from pathlib import Path


PCAP_ENDIAN = {
    b"\xd4\xc3\xb2\xa1": "<",
    b"\xa1\xb2\xc3\xd4": ">",
    b"\x4d\x3c\xb2\xa1": "<",
    b"\xa1\xb2\x3c\x4d": ">",
}
PRO_DJ_LINK_PORTS = {50000, 50001, 50002}


def ethernet_payload(packet: memoryview) -> tuple[int, int] | None:
    if len(packet) < 14:
        return None

    offset = 14
    ether_type = int.from_bytes(packet[12:14], "big")
    while ether_type in (0x8100, 0x88A8):
        if len(packet) < offset + 4:
            return None
        ether_type = int.from_bytes(packet[offset + 2 : offset + 4], "big")
        offset += 4

    return ether_type, offset


def keep_packet(
    packet: memoryview, peer_ip: bytes, control_only: bool = False
) -> bool:
    payload = ethernet_payload(packet)
    if payload is None:
        return False

    ether_type, offset = payload
    if ether_type == 0x0806:
        return peer_ip in packet[offset:]

    if ether_type != 0x0800 or len(packet) < offset + 20:
        return False

    version_ihl = packet[offset]
    if version_ihl >> 4 != 4:
        return False

    header_length = (version_ihl & 0x0F) * 4
    if header_length < 20 or len(packet) < offset + header_length:
        return False

    source = bytes(packet[offset + 12 : offset + 16])
    destination = bytes(packet[offset + 16 : offset + 20])
    if not control_only and peer_ip in (source, destination):
        return True

    if packet[offset + 9] != 17:
        return False

    fragment = int.from_bytes(packet[offset + 6 : offset + 8], "big")
    if fragment & 0x1FFF:
        return False

    udp_offset = offset + header_length
    if len(packet) < udp_offset + 4:
        return False

    source_port, destination_port = struct.unpack_from(">HH", packet, udp_offset)
    return source_port in PRO_DJ_LINK_PORTS or destination_port in PRO_DJ_LINK_PORTS


def filter_capture(
    source: Path, destination: Path, peer: str, control_only: bool = False
) -> tuple[int, int]:
    peer_ip = ipaddress.IPv4Address(peer).packed

    with source.open("rb") as input_file, destination.open("wb") as output_file:
        with mmap.mmap(input_file.fileno(), 0, access=mmap.ACCESS_READ) as capture:
            if len(capture) < 24:
                raise ValueError("capture is shorter than a PCAP global header")

            magic = capture[:4]
            try:
                endian = PCAP_ENDIAN[magic]
            except KeyError as error:
                raise ValueError("only classic PCAP input is supported") from error

            _, _, _, _, _, _, link_type = struct.unpack_from(
                f"{endian}IHHIIII", capture, 0
            )
            if link_type != 1:
                raise ValueError(f"Ethernet PCAP required, got link type {link_type}")

            output_file.write(capture[:24])
            offset = 24
            read_count = 0
            write_count = 0

            while offset < len(capture):
                if len(capture) - offset < 16:
                    raise ValueError("truncated PCAP record header")

                _, _, captured_length, _ = struct.unpack_from(
                    f"{endian}IIII", capture, offset
                )
                packet_end = offset + 16 + captured_length
                if packet_end > len(capture):
                    print(
                        f"{source}: ignored incomplete final PCAP packet",
                        flush=True,
                    )
                    break

                packet = memoryview(capture)[offset + 16 : packet_end]
                read_count += 1
                if keep_packet(packet, peer_ip, control_only):
                    output_file.write(capture[offset:packet_end])
                    write_count += 1
                packet.release()
                offset = packet_end

    return read_count, write_count


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--peer", required=True)
    parser.add_argument("--control-only", action="store_true")
    args = parser.parse_args()

    read_count, write_count = filter_capture(
        args.source, args.destination, args.peer, args.control_only
    )
    print(f"{args.source}: kept {write_count} of {read_count} packets")


if __name__ == "__main__":
    main()
