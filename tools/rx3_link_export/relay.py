#!/usr/bin/env python3
"""Relay Pro DJ Link broadcasts and maintain the RX3 USB-MIDI PC gate."""

from __future__ import annotations

import argparse
import errno
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
DBSERVER_QUERY_PORT = 12523
DBSERVER_QUERY = b"\x00\x00\x00\x0fRemoteDBServer\x00"
IP_PKTINFO = 8
ACTIVATE_PC_CONTROL = bytes.fromhex("f00040050000030d005001f7")
INITIALIZE_PC_CONTROL = (
    bytes.fromhex("f0f7"),
    bytes.fromhex(
        "f07e7f0d7002351107617f7f7f7f11000001000100020000001c0000010000f7"
    ),
)
STOCK_RX3_ANNOUNCEMENT = bytes.fromhex(
    "5173707431576d4a4f4c060058444a2d5258330000000000000000000000000001"
    "0300360b02c83dfc16af99a9feaf99010000000700"
)
RX3_IDLE_STATUS = (
    bytes.fromhex(
        "5173707431576d4a4f4c0a58444a2d5258330000000000000000000000000001"
        "050b01000b000100000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000010000000000000400000000000000000000000000000000"
        "00000000000000000080009e001000007fffffff8000ffff00000000000000ff"
        "ffffffff01ff0000000000000000000000000000000001000000000000000000"
        "0000000000000000000000001f01000000000000000000000100000000000000"
        "0000000000000000000000000000000012345678000000010101010101010000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000"
    ),
    bytes.fromhex(
        "5173707431576d4a4f4c0a58444a2d5258330000000000000000000000000001"
        "050c01000c000100000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000400000000000000000000000000000000"
        "00000000000000000080009e001000007fffffff8000ffff00000000000000ff"
        "ffffffff01ff0000000000000000000000000000000001000000000000000000"
        "0000000000000000000000001f01000000000000000000000100000000000000"
        "0000000000000000000000000000000000000000000000000100000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000"
        "00000000"
    ),
)
RX3_OPERATING_TRANSITION = (
    bytes.fromhex(
        "5173707431576d4a4f4c3058444a2d5258330000000000000000000000000001"
        "030b0000"
    ),
    bytes.fromhex(
        "5173707431576d4a4f4c1058444a2d5258330000000000000000000000000001"
        "000b0000"
    ),
    bytes.fromhex(
        "5173707431576d4a4f4c4658444a2d5258330000000000000000000000000001"
        "000b00040b040000"
    ),
)


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
    rx3_mac: bytes
    usb_rekordbox_mac: bytes
    lan_output_interface: str | None = None
    emulate_rx3: bool = False


@dataclass
class RelayState:
    rekordbox_device_id: int | None = None


def typed_field_size(data: bytes, offset: int) -> int | None:
    if offset >= len(data):
        return None

    kind = data[offset]
    fixed_sizes = {0x0F: 2, 0x10: 3, 0x11: 5}
    if kind in fixed_sizes:
        return fixed_sizes[kind]
    if kind not in (0x14, 0x26) or len(data) < offset + 5:
        raise ValueError(f"invalid dbserver field type 0x{kind:02x}")

    length = int.from_bytes(data[offset + 1 : offset + 5], "big")
    return 5 + length * (2 if kind == 0x26 else 1)


def dbserver_message_size(data: bytes) -> int | None:
    offset = 0
    for _ in range(4):
        size = typed_field_size(data, offset)
        if size is None or len(data) < offset + size:
            return None
        offset += size

    arg_list_size = typed_field_size(data, offset)
    if arg_list_size is None or len(data) < offset + arg_list_size:
        return None
    if data[offset] != 0x14:
        raise ValueError("dbserver message argument list is not binary")

    argument_count = data[14]
    argument_types = data[offset + 5 : offset + arg_list_size]
    offset += arg_list_size
    field_types = {0x02: 0x26, 0x03: 0x14, 0x06: 0x11}
    for index in range(argument_count):
        if index >= len(argument_types) or argument_types[index] not in field_types:
            raise ValueError("invalid dbserver argument type list")
        expected_type = field_types[argument_types[index]]
        if offset >= len(data):
            return None
        if data[offset] != expected_type:
            raise ValueError("dbserver argument does not match its declared type")
        size = typed_field_size(data, offset)
        if size is None or len(data) < offset + size:
            return None
        offset += size

    return offset


def normalize_initial_dbserver_response(
    data: bytes, source_device_id: int | None, device_id: int = 0x11
) -> bytes:
    size = dbserver_message_size(data)
    if size is None:
        raise ValueError("incomplete dbserver response")

    normalized = bytearray(data)
    final_field = size - 5
    if final_field < 0 or normalized[final_field] != 0x11:
        return data

    response_device_id = int.from_bytes(normalized[final_field + 1 : size], "big")
    is_learned_identity = response_device_id == source_device_id
    is_lan_pc_identity = (
        source_device_id is None and 0x29 <= response_device_id <= 0x2C
    )
    if is_learned_identity or is_lan_pc_identity:
        normalized[final_field + 1 : size] = device_id.to_bytes(4, "big")

    return bytes(normalized)


class InitialDbserverResponseNormalizer:
    def __init__(self, state: RelayState) -> None:
        self.state = state
        self.buffer = bytearray()
        self.greeting_remaining = 5
        self.complete = False

    def feed(self, data: bytes) -> bytes:
        if self.complete:
            return data

        self.buffer.extend(data)
        output = bytearray()
        if self.greeting_remaining:
            count = min(self.greeting_remaining, len(self.buffer))
            output.extend(self.buffer[:count])
            del self.buffer[:count]
            self.greeting_remaining -= count
            if self.greeting_remaining:
                return bytes(output)

        try:
            size = dbserver_message_size(self.buffer)
        except ValueError:
            self.complete = True
            output.extend(self.buffer)
            self.buffer.clear()
            return bytes(output)
        if size is None:
            return bytes(output)

        message = bytes(self.buffer[:size])
        del self.buffer[:size]
        normalized = normalize_initial_dbserver_response(
            message, self.state.rekordbox_device_id
        )
        if normalized != message:
            source_device_id = int.from_bytes(message[size - 4 : size], "big")
            print(
                f"dbserver identity normalized: 0x{source_device_id:02x} -> 0x11",
                flush=True,
            )
        output.extend(normalized)
        output.extend(self.buffer)
        self.buffer.clear()
        self.complete = True
        return bytes(output)


def recv_exact(connection: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = connection.recv(remaining)
        if not chunk:
            raise ConnectionError("connection closed before expected data arrived")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def proxy_connections(
    client: socket.socket, upstream: socket.socket, state: RelayState
) -> None:
    normalizer = InitialDbserverResponseNormalizer(state)
    sockets = (client, upstream)
    try:
        while True:
            readable, _, _ = select.select(sockets, (), ())
            for source in readable:
                destination = upstream if source is client else client
                data = source.recv(65536)
                if not data:
                    return
                if source is upstream:
                    data = normalizer.feed(data)
                if data:
                    destination.sendall(data)
    finally:
        client.close()
        upstream.close()


class DbServerBroker:
    def __init__(
        self,
        config: RelayConfig,
        state: RelayState,
        query_port: int = DBSERVER_QUERY_PORT,
    ) -> None:
        self.config = config
        self.state = state
        self.query_port = query_port
        self.dynamic_listeners: dict[int, socket.socket] = {}
        self.lock = threading.Lock()

    def make_tcp_listener(self, port: int) -> socket.socket:
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind((self.config.usb_rekordbox_ip, port))
        listener.listen()
        return listener

    def connect_upstream(self, port: int) -> socket.socket:
        connection = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            if self.config.lan_output_interface:
                connection.setsockopt(
                    socket.SOL_SOCKET,
                    socket.SO_BINDTODEVICE,
                    self.config.lan_output_interface.encode() + b"\0",
                )
            connection.bind((self.config.lan_rx3_ip, 0))
            connection.connect((self.config.rekordbox_ip, port))
            return connection
        except BaseException:
            connection.close()
            raise

    def ensure_dynamic_listener(self, port: int) -> None:
        with self.lock:
            if port in self.dynamic_listeners:
                return
            listener = self.make_tcp_listener(port)
            self.dynamic_listeners[port] = listener

        threading.Thread(
            target=self.serve_dynamic_port,
            args=(port, listener),
            daemon=True,
        ).start()

    def serve_dynamic_port(self, port: int, listener: socket.socket) -> None:
        while True:
            client, _ = listener.accept()
            try:
                upstream = self.connect_upstream(port)
            except OSError:
                client.close()
                continue
            threading.Thread(
                target=proxy_connections,
                args=(client, upstream, self.state),
                daemon=True,
            ).start()

    def handle_query(self, client: socket.socket) -> None:
        try:
            with self.connect_upstream(self.query_port) as upstream:
                header = recv_exact(client, 4)
                length = int.from_bytes(header, "big")
                request = header + recv_exact(client, length)
                if request != DBSERVER_QUERY:
                    raise ValueError("unexpected dbserver port query")
                upstream.sendall(request)
                response = recv_exact(upstream, 2)

            port = int.from_bytes(response, "big")
            if port not in (0, 0xFFFF):
                self.ensure_dynamic_listener(port)
                print(f"dbserver dynamic port ready: {port}", flush=True)
            client.sendall(response)
        except (ConnectionError, OSError, ValueError) as error:
            print(f"dbserver query failed: {error}", flush=True)
        finally:
            client.close()

    def serve(self) -> None:
        listener = self.make_tcp_listener(self.query_port)
        print(
            f"dbserver broker ready: {self.config.usb_rekordbox_ip}:"
            f"{self.query_port} -> {self.config.rekordbox_ip}",
            flush=True,
        )
        while True:
            client, _ = listener.accept()
            threading.Thread(
                target=self.handle_query, args=(client,), daemon=True
            ).start()


def translate_addresses(data: bytes, source: str, replacement: str) -> bytes:
    return data.replace(socket.inet_aton(source), socket.inet_aton(replacement))


def advertised_mac(data: bytes) -> bytes | None:
    if not data.startswith(PRO_DJ_LINK_MAGIC) or len(data) < 44:
        return None

    kind = data[10]
    if kind == 0x00:
        return data[38:44]
    if kind == 0x06 and len(data) == 54:
        return data[38:44]
    if kind == 0x02 and len(data) >= 46:
        return data[40:46]

    return None


def advertised_ip(data: bytes) -> str | None:
    if not data.startswith(PRO_DJ_LINK_MAGIC) or len(data) < 40:
        return None

    kind = data[10]
    if kind in (0x02, 0x05):
        value = data[36:40]
    elif kind == 0x06 and len(data) == 54:
        value = data[44:48]
    else:
        return None

    address = socket.inet_ntoa(value)
    if not address.startswith("169.254."):
        return None

    return address


def translate_identity(
    data: bytes,
    source_ip: str,
    replacement_ip: str,
    source_mac: bytes | None,
    replacement_mac: bytes,
) -> bytes:
    translated = translate_addresses(data, source_ip, replacement_ip)
    if source_mac:
        translated = translated.replace(source_mac, replacement_mac)

    return translated


def normalize_rekordbox_device_id(
    data: bytes, source_device_id: int, device_id: int = 0x11
) -> bytes:
    if not data.startswith(PRO_DJ_LINK_MAGIC) or len(data) < 37:
        return data

    normalized = bytearray(data)
    if normalized[33] == source_device_id:
        normalized[33] = device_id

    kind = normalized[10]
    if kind == 0x06 and len(normalized) == 54:
        normalized[36] = device_id
    elif kind in (0x11, 0x29, 0x47) and normalized[36] == source_device_id:
        normalized[36] = device_id

    return bytes(normalized)


def packet_type(data: bytes) -> str:
    if len(data) > 10 and data.startswith(PRO_DJ_LINK_MAGIC):
        return f"0x{data[10]:02x}"
    return "unknown"


def make_rx3_announcement(config: RelayConfig) -> bytes:
    announcement = bytearray(STOCK_RX3_ANNOUNCEMENT)
    announcement[38:44] = config.rx3_mac
    announcement[44:48] = socket.inet_aton(config.lan_rx3_ip)
    return bytes(announcement)


def make_discovery_packet(kind: int, payload: bytes) -> bytes:
    name = b"XDJ-RX3" + bytes(13)
    size = 36 + len(payload)
    return b"".join(
        (
            PRO_DJ_LINK_MAGIC,
            bytes((kind, 0)),
            name,
            b"\x01\x03",
            size.to_bytes(2, "big"),
            payload,
        )
    )


def rx3_claim_sequence(config: RelayConfig) -> list[tuple[float, bytes]]:
    events: list[tuple[float, bytes]] = []
    for index in range(3):
        events.append((index * 0.3, make_discovery_packet(0x0A, b"\x07")))
        events.append(
            (
                1.0 + index * 0.3,
                make_discovery_packet(
                    0x00, bytes((index + 1, 7)) + config.rx3_mac
                ),
            )
        )

    offset = 2.0
    for candidate in (0x0B, 0x01, 0x02, 0x03, 0x04, 0x21):
        for counter in range(1, 4):
            payload = b"".join(
                (
                    socket.inet_aton(config.lan_rx3_ip),
                    config.rx3_mac,
                    bytes((candidate, counter, 7, 2)),
                )
            )
            events.append((offset, make_discovery_packet(0x02, payload)))
            offset += 0.3

    for index in range(3):
        events.append(
            (
                7.5 + index * 0.3,
                make_discovery_packet(0x04, bytes((0x0B, index + 1))),
            )
        )
    return sorted(events)


def packet_context(
    ancillary: list[tuple[int, int, bytes]],
) -> tuple[int, str]:
    for level, kind, value in ancillary:
        if level == socket.IPPROTO_IP and kind == IP_PKTINFO:
            interface, _, destination = struct.unpack("=I4s4s", value[:12])
            return interface, socket.inet_ntoa(destination)
    raise RuntimeError("packet arrived without IP_PKTINFO")


def send_broadcast(
    data: bytes,
    interface: str,
    source: str,
    destination: str,
    port: int,
    source_port: int = 0,
) -> None:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as outgoing:
        outgoing.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        outgoing.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        try:
            outgoing.bind((source, source_port))
        except OSError as error:
            if error.errno != errno.EADDRINUSE or source_port == 0:
                raise
            outgoing.bind((source, 0))
        packet_info = struct.pack(
            "=I4s4s",
            socket.if_nametoindex(interface),
            socket.inet_aton(source),
            b"\0" * 4,
        )
        outgoing.sendmsg(
            [data],
            [(socket.IPPROTO_IP, IP_PKTINFO, packet_info)],
            0,
            (destination, port),
        )


def make_listener(port: int) -> socket.socket:
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    listener.setsockopt(socket.IPPROTO_IP, IP_PKTINFO, 1)
    listener.bind(("0.0.0.0", port))
    return listener


def interface_index(name: str) -> int | None:
    try:
        return socket.if_nametoindex(name)
    except OSError:
        return None


def interface_mac(name: str) -> bytes:
    value = (Path("/sys/class/net") / name / "address").read_text().strip()
    return bytes.fromhex(value.replace(":", ""))


def relay_broadcasts(config: RelayConfig, state: RelayState | None = None) -> None:
    state = state or RelayState()
    listeners = {make_listener(port): port for port in PRO_DJ_LINK_PORTS}
    lan_output_interface = config.lan_output_interface or config.lan_interface
    lan_index = interface_index(config.lan_interface)
    lan_output_index = interface_index(lan_output_interface)
    usb_index = interface_index(config.usb_interface)
    fallback_announcement = make_rx3_announcement(config)
    sequence_started = time.monotonic() + 0.5
    claim_events = (
        [
            (sequence_started + delay, packet)
            for delay, packet in rx3_claim_sequence(config)
        ]
        if config.emulate_rx3
        else []
    )
    operating_transition_sent = False
    rx3_announced = False
    rekordbox_mac: bytes | None = None
    rx3_advertised_ip: str | None = None
    next_status: float | None = None
    next_fallback = sequence_started + 10.2 if config.emulate_rx3 else None
    rx3_ip = config.rx3_ip

    print(
        f"broadcast relay ready: {config.lan_interface} <-> "
        f"{config.usb_interface}",
        flush=True,
    )

    while True:
        now = time.monotonic()
        current_lan_index = interface_index(config.lan_interface)
        current_lan_output_index = interface_index(lan_output_interface)
        current_usb_index = interface_index(config.usb_interface)
        if (
            current_lan_index is None
            or current_lan_output_index is None
            or current_usb_index is None
        ):
            lan_index = current_lan_index
            lan_output_index = current_lan_output_index
            usb_index = current_usb_index
            time.sleep(0.5)
            continue
        if (
            current_lan_index != lan_index
            or current_lan_output_index != lan_output_index
            or current_usb_index != usb_index
        ):
            lan_index = current_lan_index
            lan_output_index = current_lan_output_index
            usb_index = current_usb_index
            print(
                f"relay interfaces ready: {config.lan_interface}={lan_index} "
                f"{config.usb_interface}={usb_index}",
                flush=True,
            )

        while claim_events and now >= claim_events[0][0]:
            _, packet = claim_events.pop(0)
            send_broadcast(
                packet,
                lan_output_interface,
                config.lan_rx3_ip,
                config.lan_broadcast,
                50000,
            )

        if next_status is not None and now >= next_status:
            send_broadcast(
                RX3_IDLE_STATUS[0],
                lan_output_interface,
                config.lan_rx3_ip,
                config.lan_broadcast,
                50002,
            )
            time.sleep(0.008)
            send_broadcast(
                RX3_IDLE_STATUS[1],
                lan_output_interface,
                config.lan_rx3_ip,
                config.lan_broadcast,
                50002,
            )
            next_status = now + 0.214

        if next_fallback is not None and now >= next_fallback:
            send_broadcast(
                fallback_announcement,
                lan_output_interface,
                config.lan_rx3_ip,
                config.lan_broadcast,
                50000,
            )
            rx3_announced = True
            next_fallback = now + 2.0

        readable, _, _ = select.select(listeners, (), (), 0.5)
        for listener in readable:
            port = listeners[listener]
            data, ancillary, _, peer = listener.recvmsg(65535, socket.CMSG_SPACE(12))
            if not data.startswith(PRO_DJ_LINK_MAGIC):
                continue

            interface, destination = packet_context(ancillary)
            if (
                interface in (lan_index, lan_output_index)
                and peer[0] == config.rekordbox_ip
            ):
                if not rx3_announced:
                    continue
                announced_mac = advertised_mac(data)
                if announced_mac and announced_mac != rekordbox_mac:
                    rekordbox_mac = announced_mac
                    print(
                        "Rekordbox MAC learned: "
                        f"{rekordbox_mac.hex(':')} -> "
                        f"{config.usb_rekordbox_mac.hex(':')}",
                        flush=True,
                    )
                if data[10] == 0x06 and len(data) == 54:
                    state.rekordbox_device_id = data[36]
                translated = translate_identity(
                    data,
                    config.rekordbox_ip,
                    config.usb_rekordbox_ip,
                    rekordbox_mac,
                    config.usb_rekordbox_mac,
                )
                if state.rekordbox_device_id is not None:
                    translated = normalize_rekordbox_device_id(
                        translated, state.rekordbox_device_id
                    )
                if data[10] in (0x11, 0x31, 0x47):
                    print(
                        f"Rekordbox handshake {packet_type(data)} "
                        f"raw={data.hex()} translated={translated.hex()}",
                        flush=True,
                    )
                outgoing_destination = (
                    rx3_ip
                    if destination == config.lan_rx3_ip
                    else config.usb_broadcast
                )
                try:
                    send_broadcast(
                        translated,
                        config.usb_interface,
                        config.usb_rekordbox_ip,
                        outgoing_destination,
                        port,
                        peer[1],
                    )
                except OSError:
                    continue
                direction = "rekordbox->rx3"
                kind = data[10]
                if (
                    config.emulate_rx3
                    and kind == 0x06
                    and rx3_announced
                    and not operating_transition_sent
                ):
                    send_broadcast(
                        RX3_OPERATING_TRANSITION[0],
                        lan_output_interface,
                        config.lan_rx3_ip,
                        config.lan_broadcast,
                        50002,
                    )
                    time.sleep(0.015)
                    send_broadcast(
                        RX3_OPERATING_TRANSITION[1],
                        lan_output_interface,
                        config.lan_rx3_ip,
                        config.lan_broadcast,
                        50002,
                    )
                    time.sleep(0.002)
                    send_broadcast(
                        RX3_IDLE_STATUS[0],
                        lan_output_interface,
                        config.lan_rx3_ip,
                        config.lan_broadcast,
                        50002,
                    )
                    time.sleep(0.008)
                    send_broadcast(
                        RX3_IDLE_STATUS[1],
                        lan_output_interface,
                        config.lan_rx3_ip,
                        config.lan_broadcast,
                        50002,
                    )
                    time.sleep(0.007)
                    send_broadcast(
                        RX3_OPERATING_TRANSITION[2],
                        lan_output_interface,
                        config.lan_rx3_ip,
                        config.lan_broadcast,
                        50002,
                    )
                    operating_transition_sent = True
                    next_status = time.monotonic() + 0.183
                    print(
                        "rekordbox claim complete; RX3 operating stream started",
                        flush=True,
                    )
            elif (
                interface == usb_index
                and peer[0].startswith("169.254.")
                and peer[0] != config.usb_rekordbox_ip
            ):
                rx3_announced = True
                if peer[0] != rx3_ip:
                    rx3_ip = peer[0]
                    print(f"RX3 link-local address learned: {rx3_ip}", flush=True)
                announced_ip = advertised_ip(data)
                if announced_ip and announced_ip != rx3_advertised_ip:
                    rx3_advertised_ip = announced_ip
                    print(
                        f"RX3 advertised address learned: {rx3_advertised_ip} -> "
                        f"{config.lan_rx3_ip}",
                        flush=True,
                    )
                translated = translate_addresses(
                    data, rx3_ip, config.lan_rx3_ip
                )
                if rx3_advertised_ip is not None:
                    translated = translate_addresses(
                        translated, rx3_advertised_ip, config.lan_rx3_ip
                    )
                if data[10] in (0x10, 0x30, 0x46):
                    print(
                        f"RX3 handshake {packet_type(data)} "
                        f"source={peer[0]}:{peer[1]} destination={destination}:{port} "
                        f"raw={data.hex()} translated={translated.hex()}",
                        flush=True,
                    )
                outgoing_destination = (
                    config.rekordbox_ip
                    if destination == config.usb_rekordbox_ip
                    else config.lan_broadcast
                )
                send_broadcast(
                    translated,
                    lan_output_interface,
                    config.lan_rx3_ip,
                    outgoing_destination,
                    port,
                    peer[1],
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
                for message in INITIALIZE_PC_CONTROL:
                    midi.write(message)
                print("USB-MIDI host initialization sent", flush=True)
                time.sleep(5.15)
                while True:
                    midi.write(ACTIVATE_PC_CONTROL)
                    time.sleep(0.2)
        except (OSError, RuntimeError) as error:
            print(f"USB-MIDI activation waiting: {error}", flush=True)
            time.sleep(2)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lan-interface", default="lan0")
    parser.add_argument("--lan-output-interface")
    parser.add_argument("--usb-interface", required=True)
    parser.add_argument("--rekordbox-ip", required=True)
    parser.add_argument("--rx3-ip", default="169.254.100.2")
    parser.add_argument("--lan-rx3-ip", default="10.0.0.253")
    parser.add_argument("--usb-rekordbox-ip", default="169.254.100.1")
    parser.add_argument("--lan-broadcast", default="10.0.0.255")
    parser.add_argument("--usb-broadcast", default="169.254.255.255")
    parser.add_argument("--rx3-mac", default="c8:3d:fc:16:af:99")
    parser.add_argument("--usb-rekordbox-mac")
    parser.add_argument("--midi-device")
    parser.add_argument("--without-midi", action="store_true")
    parser.add_argument("--emulate-rx3", action="store_true")
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
        rx3_mac=bytes.fromhex(args.rx3_mac.replace(":", "")),
        usb_rekordbox_mac=(
            bytes.fromhex(args.usb_rekordbox_mac.replace(":", ""))
            if args.usb_rekordbox_mac
            else interface_mac(args.usb_interface)
        ),
        lan_output_interface=args.lan_output_interface,
        emulate_rx3=args.emulate_rx3,
    )
    state = RelayState()
    if not args.without_midi:
        threading.Thread(
            target=maintain_pc_control,
            args=(args.midi_device,),
            daemon=True,
        ).start()
    threading.Thread(
        target=DbServerBroker(config, state).serve,
        daemon=True,
    ).start()
    relay_broadcasts(config, state)


if __name__ == "__main__":
    main()
