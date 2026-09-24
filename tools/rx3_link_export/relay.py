#!/usr/bin/env python3
"""Relay Pro DJ Link broadcasts and maintain the RX3 USB-MIDI PC gate."""

from __future__ import annotations

import argparse
import glob
import select
import socket
import struct
import threading
import time
from dataclasses import dataclass
from pathlib import Path


PRO_DJ_LINK_MAGIC = b"Qspt1WmJOL"
PRO_DJ_LINK_PORTS = (50000, 50001, 50002)
IP_PKTINFO = 8
ACTIVATE_PC_CONTROL = bytes.fromhex("f00040050000030d005001f7")


@dataclass(frozen=True)
class RelayConfig:
    lan_interface: str
    usb_interface: str
    rekordbox_ip: str
    rx3_ip: str
    lan_rx3_ip: str
    usb_rekordbox_ip: str
    lan_broadcast: str
    usb_broadcast: str


def translate_addresses(data: bytes, source: str, replacement: str) -> bytes:
    return data.replace(socket.inet_aton(source), socket.inet_aton(replacement))


def packet_type(data: bytes) -> str:
    if len(data) > 10 and data.startswith(PRO_DJ_LINK_MAGIC):
        return f"0x{data[10]:02x}"
    return "unknown"


def packet_interface(ancillary: list[tuple[int, int, bytes]]) -> int:
    for level, kind, value in ancillary:
        if level == socket.IPPROTO_IP and kind == IP_PKTINFO:
            return struct.unpack("=I4s4s", value[:12])[0]
    raise RuntimeError("packet arrived without IP_PKTINFO")


def send_broadcast(data: bytes, source: str, destination: str, port: int) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as outgoing:
        outgoing.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        outgoing.bind((source, 0))
        outgoing.sendto(data, (destination, port))


def make_listener(port: int) -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    listener.setsockopt(socket.IPPROTO_IP, IP_PKTINFO, 1)
    listener.bind(("0.0.0.0", port))
    return listener


def relay_broadcasts(config: RelayConfig) -> None:
    listeners = {make_listener(port): port for port in PRO_DJ_LINK_PORTS}
    lan_index = socket.if_nametoindex(config.lan_interface)
    usb_index = socket.if_nametoindex(config.usb_interface)

    print(
        f"broadcast relay ready: {config.lan_interface} <-> "
        f"{config.usb_interface}",
        flush=True,
    )

    while True:
        readable, _, _ = select.select(listeners, (), (), 1.0)
        for listener in readable:
            port = listeners[listener]
            data, ancillary, _, peer = listener.recvmsg(65535, socket.CMSG_SPACE(12))
            if not data.startswith(PRO_DJ_LINK_MAGIC):
                continue

            interface = packet_interface(ancillary)
            if interface == lan_index and peer[0] == config.rekordbox_ip:
                translated = translate_addresses(
                    data, config.rekordbox_ip, config.usb_rekordbox_ip
                )
                send_broadcast(
                    translated,
                    config.usb_rekordbox_ip,
                    config.usb_broadcast,
                    port,
                )
                direction = "rekordbox->rx3"
            elif interface == usb_index and peer[0] == config.rx3_ip:
                translated = translate_addresses(
                    data, config.rx3_ip, config.lan_rx3_ip
                )
                send_broadcast(
                    translated,
                    config.lan_rx3_ip,
                    config.lan_broadcast,
                    port,
                )
                direction = "rx3->rekordbox"
            else:
                continue

            print(
                f"{direction} udp/{port} type={packet_type(data)} "
                f"bytes={len(data)}",
                flush=True,
            )


def usb_identity(device: Path) -> str:
    current = (Path("/sys/class/sound") / device.name / "device").resolve()
    values: list[str] = []
    for parent in (current, *current.parents):
        for name in ("product", "manufacturer", "idVendor", "idProduct"):
            candidate = parent / name
            try:
                values.append(candidate.read_text(errors="replace").strip())
            except OSError:
                pass
    return " ".join(values).lower()


def find_rx3_midi_device(explicit: str | None) -> Path:
    if explicit:
        device = Path(explicit)
        if not device.exists():
            raise FileNotFoundError(device)
        return device

    candidates = [Path(path) for path in glob.glob("/dev/snd/midiC*D*")]
    matches = [
        device
        for device in candidates
        if any(token in usb_identity(device) for token in ("xdj-rx3", "pioneer dj"))
    ]
    if len(matches) == 1:
        return matches[0]
    if not matches and len(candidates) == 1:
        return candidates[0]

    names = ", ".join(map(str, candidates)) or "none"
    raise RuntimeError(
        "could not uniquely identify the RX3 raw-MIDI endpoint; "
        f"candidates: {names}; pass --midi-device"
    )


def maintain_pc_control(device_name: str | None) -> None:
    while True:
        try:
            device = find_rx3_midi_device(device_name)
            print(f"USB-MIDI activation ready: {device}", flush=True)
            with device.open("wb", buffering=0) as midi:
                while True:
                    midi.write(ACTIVATE_PC_CONTROL)
                    time.sleep(0.2)
        except (OSError, RuntimeError) as error:
            print(f"USB-MIDI activation waiting: {error}", flush=True)
            time.sleep(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lan-interface", default="lan0")
    parser.add_argument("--usb-interface", required=True)
    parser.add_argument("--rekordbox-ip", required=True)
    parser.add_argument("--rx3-ip", required=True)
    parser.add_argument("--lan-rx3-ip", default="10.0.0.253")
    parser.add_argument("--usb-rekordbox-ip", default="169.254.100.1")
    parser.add_argument("--lan-broadcast", default="10.0.0.255")
    parser.add_argument("--usb-broadcast", default="169.254.255.255")
    parser.add_argument("--midi-device")
    parser.add_argument("--without-midi", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    config = RelayConfig(
        lan_interface=args.lan_interface,
        usb_interface=args.usb_interface,
        rekordbox_ip=args.rekordbox_ip,
        rx3_ip=args.rx3_ip,
        lan_rx3_ip=args.lan_rx3_ip,
        usb_rekordbox_ip=args.usb_rekordbox_ip,
        lan_broadcast=args.lan_broadcast,
        usb_broadcast=args.usb_broadcast,
    )
    if not args.without_midi:
        threading.Thread(
            target=maintain_pc_control,
            args=(args.midi_device,),
            daemon=True,
        ).start()
    relay_broadcasts(config)


if __name__ == "__main__":
    main()
