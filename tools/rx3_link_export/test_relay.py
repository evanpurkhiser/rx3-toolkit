import socket
import threading
import unittest

from tools.rx3_link_export.relay import (
    ACTIVATE_PC_CONTROL,
    DBSERVER_QUERY,
    INITIALIZE_PC_CONTROL,
    PRO_DJ_LINK_MAGIC,
    RX3_IDLE_STATUS,
    DbServerBroker,
    InitialDbserverResponseNormalizer,
    RelayConfig,
    RelayState,
    advertised_mac,
    advertised_ip,
    dbserver_message_size,
    make_rx3_announcement,
    normalize_rekordbox_device_id,
    normalize_initial_dbserver_response,
    packet_type,
    rx3_claim_sequence,
    translate_addresses,
    translate_identity,
)


def dbserver_message(*values: int) -> bytes:
    argument_types = bytes([0x06] * len(values) + [0] * (12 - len(values)))
    fields = [
        b"\x11\x87\x23\x49\xae",
        b"\x11\xff\xff\xff\xfe",
        b"\x10\x40\x00",
        bytes((0x0F, len(values))),
        b"\x14\x00\x00\x00\x0c" + argument_types,
    ]
    fields.extend(b"\x11" + value.to_bytes(4, "big") for value in values)
    return b"".join(fields)


def test_config(rekordbox_ip: str = "127.0.0.1") -> RelayConfig:
    return RelayConfig(
        lan_interface="lan0",
        usb_interface="usb0",
        rekordbox_ip=rekordbox_ip,
        rx3_ip="169.254.175.153",
        lan_rx3_ip="127.0.0.2",
        usb_rekordbox_ip="127.0.0.2",
        lan_broadcast="10.0.0.255",
        usb_broadcast="169.254.255.255",
        rx3_mac=bytes.fromhex("c83dfc16af99"),
        usb_rekordbox_mac=bytes.fromhex("c83dfc16af9a"),
    )


class RelayTests(unittest.TestCase):
    def test_parses_complete_dbserver_message_across_partial_buffers(self):
        message = dbserver_message(1, 0x29)

        self.assertIsNone(dbserver_message_size(message[:-1]))
        self.assertEqual(dbserver_message_size(message), len(message))

    def test_normalizes_only_final_typed_integer_in_initial_response(self):
        message = dbserver_message(0x29, 0x29)
        subsequent_track_data = b"track\x11\x00\x00\x00\x29data"

        normalized = normalize_initial_dbserver_response(
            message + subsequent_track_data, 0x29
        )

        track_offset = -len(subsequent_track_data)
        self.assertEqual(normalized[track_offset:], subsequent_track_data)
        self.assertEqual(normalized[track_offset - 9 : track_offset - 5], b"\0\0\0\x29")
        self.assertEqual(normalized[track_offset - 4 : track_offset], b"\0\0\0\x11")

    def test_normalizes_lan_pc_identity_before_udp_identity_is_learned(self):
        message = dbserver_message(1, 0x29)

        self.assertEqual(
            normalize_initial_dbserver_response(message, None),
            dbserver_message(1, 0x11),
        )
        self.assertEqual(
            normalize_initial_dbserver_response(dbserver_message(1, 0x11), None),
            dbserver_message(1, 0x11),
        )

    def test_stream_normalizer_handles_fragmented_greeting_and_response_once(self):
        state = RelayState(rekordbox_device_id=0x29)
        normalizer = InitialDbserverResponseNormalizer(state)
        message = dbserver_message(1, 0x29)
        later = dbserver_message(2, 0x29)

        chunks = [b"\x11\x00", b"\x00\x00\x01" + message[:11], message[11:] + later]
        result = b"".join(normalizer.feed(chunk) for chunk in chunks)

        expected = b"\x11\x00\x00\x00\x01" + dbserver_message(1, 0x11) + later
        self.assertEqual(result, expected)

    def test_broker_opens_dynamic_listener_before_returning_port(self):
        query_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        query_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        query_listener.bind(("127.0.0.1", 0))
        query_listener.listen()
        query_port = query_listener.getsockname()[1]

        dynamic_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        dynamic_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        dynamic_listener.bind(("127.0.0.1", 0))
        dynamic_listener.listen()
        dynamic_port = dynamic_listener.getsockname()[1]

        response = dbserver_message(1, 0x29)
        upstream_errors: list[BaseException] = []
        upstream_peers: list[str] = []

        def serve_query() -> None:
            try:
                connection, peer = query_listener.accept()
                upstream_peers.append(peer[0])
                with connection:
                    self.assertEqual(
                        connection.recv(len(DBSERVER_QUERY)), DBSERVER_QUERY
                    )
                    connection.sendall(dynamic_port.to_bytes(2, "big"))
            except BaseException as error:
                upstream_errors.append(error)

        def serve_dynamic() -> None:
            try:
                connection, peer = dynamic_listener.accept()
                upstream_peers.append(peer[0])
                with connection:
                    self.assertEqual(connection.recv(7), b"request")
                    connection.sendall(b"\x11\x00\x00\x00\x01" + response)
            except BaseException as error:
                upstream_errors.append(error)

        threading.Thread(target=serve_query, daemon=True).start()
        threading.Thread(target=serve_dynamic, daemon=True).start()
        broker = DbServerBroker(
            test_config(), RelayState(rekordbox_device_id=0x29), query_port
        )
        threading.Thread(target=broker.serve, daemon=True).start()

        with socket.create_connection(("127.0.0.2", query_port), timeout=2) as query:
            query.sendall(DBSERVER_QUERY)
            returned_port = int.from_bytes(query.recv(2), "big")
        self.assertEqual(returned_port, dynamic_port)

        with socket.create_connection(("127.0.0.2", returned_port), timeout=2) as data:
            data.sendall(b"request")
            expected_size = 5 + len(response)
            received = bytearray()
            while len(received) < expected_size:
                received.extend(data.recv(expected_size - len(received)))

        query_listener.close()
        dynamic_listener.close()

        self.assertFalse(upstream_errors)
        self.assertEqual(upstream_peers, ["127.0.0.2", "127.0.0.2"])
        self.assertEqual(
            bytes(received),
            b"\x11\x00\x00\x00\x01" + dbserver_message(1, 0x11),
        )

    def test_pc_control_messages_match_direct_usb_capture(self):
        self.assertEqual([len(message) for message in INITIALIZE_PC_CONTROL], [2, 32])
        self.assertEqual(len(ACTIVATE_PC_CONTROL), 12)

    def test_translates_every_embedded_address(self):
        source = "10.0.0.119"
        replacement = "169.254.100.1"
        address = socket.inet_aton(source)
        packet = b"prefix" + address + b"middle" + address + b"suffix"

        translated = translate_addresses(packet, source, replacement)

        self.assertNotIn(address, translated)
        self.assertEqual(translated.count(socket.inet_aton(replacement)), 2)

    def test_translates_advertised_mac_identity(self):
        source_mac = bytes.fromhex("3ed7d4b373aa")
        replacement_mac = bytes.fromhex("c83dfc16af9a")
        packet = b"prefix" + source_mac + b"suffix"

        translated = translate_identity(
            packet,
            "10.0.0.119",
            "169.254.100.1",
            source_mac,
            replacement_mac,
        )

        self.assertNotIn(source_mac, translated)
        self.assertIn(replacement_mac, translated)

    def test_extracts_mac_from_claim_packets(self):
        mac = bytes.fromhex("3ed7d4b373aa")
        common = PRO_DJ_LINK_MAGIC + b"\x00" + bytes(27)
        self.assertEqual(advertised_mac(common + mac), mac)

        claim = PRO_DJ_LINK_MAGIC + b"\x02" + bytes(29) + mac
        self.assertEqual(advertised_mac(claim), mac)

    def test_does_not_treat_status_payload_as_an_advertised_mac(self):
        packet = bytearray(PRO_DJ_LINK_MAGIC + b"\x06" + bytes(181))
        packet[38:44] = bytes.fromhex("001100000004")

        self.assertIsNone(advertised_mac(bytes(packet)))

    def test_extracts_rx3_link_local_address_from_identity_packets(self):
        address = socket.inet_aton("169.254.175.153")
        announcement = bytearray(PRO_DJ_LINK_MAGIC + b"\x06" + bytes(43))
        announcement[44:48] = address
        claim = bytearray(PRO_DJ_LINK_MAGIC + b"\x02" + bytes(39))
        claim[36:40] = address
        request = bytearray(PRO_DJ_LINK_MAGIC + b"\x05" + bytes(37))
        request[36:40] = address

        self.assertEqual(advertised_ip(bytes(announcement)), "169.254.175.153")
        self.assertEqual(advertised_ip(bytes(claim)), "169.254.175.153")
        self.assertEqual(advertised_ip(bytes(request)), "169.254.175.153")

    def test_rejects_non_link_local_advertised_address(self):
        announcement = bytearray(PRO_DJ_LINK_MAGIC + b"\x06" + bytes(43))
        announcement[44:48] = socket.inet_aton("10.0.0.253")

        self.assertIsNone(advertised_ip(bytes(announcement)))

    def test_normalizes_lan_rekordbox_announcement_device_id(self):
        packet = PRO_DJ_LINK_MAGIC + b"\x06" + bytes(25) + b"\x29" + bytes(17)

        normalized = normalize_rekordbox_device_id(packet, 0x29)

        self.assertEqual(len(normalized), 54)
        self.assertEqual(normalized[36], 0x11)

    def test_normalizes_unicast_rekordbox_device_id_fields(self):
        packet = bytearray(PRO_DJ_LINK_MAGIC + b"\x47" + bytes(61))
        packet[33] = 0x29
        packet[36] = 0x29

        normalized = normalize_rekordbox_device_id(bytes(packet), 0x29)

        self.assertEqual(normalized[33], 0x11)
        self.assertEqual(normalized[36], 0x11)

    def test_normalizes_remote_load_response_to_direct_usb_shape(self):
        direct = bytes.fromhex(
            "5173707431576d4a4f4c4772656b6f7264626f78000000000000000000000001"
            "0111002411040000123456780000000101010301020200000000000000000000"
            "0000000000000000"
        )
        lan = bytearray(direct)
        lan[33] = 0x29
        lan[36] = 0x29

        self.assertEqual(normalize_rekordbox_device_id(bytes(lan), 0x29), direct)

    def test_reports_pro_dj_link_packet_type(self):
        self.assertEqual(packet_type(PRO_DJ_LINK_MAGIC + b"\x11payload"), "0x11")

    def test_status_packet_name_immediately_follows_type(self):
        packet = RX3_IDLE_STATUS[0]

        self.assertEqual(packet_type(packet), "0x0a")
        self.assertEqual(packet[11:31].rstrip(b"\0"), b"XDJ-RX3")

    def test_rejects_other_payloads(self):
        self.assertEqual(packet_type(b"not pro dj link"), "unknown")

    def test_fallback_announcement_matches_captured_stock_fields(self):
        config = RelayConfig(
            lan_interface="lan0",
            usb_interface="usb0",
            rekordbox_ip="10.0.0.119",
            rx3_ip="169.254.175.153",
            lan_rx3_ip="10.0.0.253",
            usb_rekordbox_ip="169.254.100.1",
            lan_broadcast="10.0.0.255",
            usb_broadcast="169.254.255.255",
            rx3_mac=bytes.fromhex("c83dfc16af99"),
            usb_rekordbox_mac=bytes.fromhex("c83dfc16af9a"),
        )

        announcement = make_rx3_announcement(config)

        self.assertEqual(len(announcement), 54)
        self.assertEqual(packet_type(announcement), "0x06")
        self.assertEqual(announcement[36:38], bytes.fromhex("0b02"))
        self.assertEqual(announcement[38:44], config.rx3_mac)
        self.assertEqual(announcement[44:48], socket.inet_aton("10.0.0.253"))
        self.assertEqual(announcement[52], 7)

    def test_claim_sequence_matches_stock_packet_shapes(self):
        config = RelayConfig(
            lan_interface="lan0",
            usb_interface="usb0",
            rekordbox_ip="10.0.0.119",
            rx3_ip="169.254.175.153",
            lan_rx3_ip="10.0.0.253",
            usb_rekordbox_ip="169.254.100.1",
            lan_broadcast="10.0.0.255",
            usb_broadcast="169.254.255.255",
            rx3_mac=bytes.fromhex("c83dfc16af99"),
            usb_rekordbox_mac=bytes.fromhex("c83dfc16af9a"),
        )

        events = rx3_claim_sequence(config)
        shapes = [(packet_type(packet), len(packet)) for _, packet in events]

        self.assertEqual(shapes[:6], [("0x0a", 37)] * 3 + [("0x00", 44)] * 3)
        self.assertEqual(shapes[6:24], [("0x02", 50)] * 18)
        self.assertEqual(shapes[24:], [("0x04", 38)] * 3)
        for _, packet in events[6:24]:
            self.assertEqual(packet[36:40], socket.inet_aton("10.0.0.253"))

    def test_idle_status_packets_match_captured_lengths_and_decks(self):
        self.assertEqual([len(packet) for packet in RX3_IDLE_STATUS], [292, 292])
        self.assertEqual([packet[32] for packet in RX3_IDLE_STATUS], [5, 5])
        self.assertEqual([packet[33] for packet in RX3_IDLE_STATUS], [11, 12])


if __name__ == "__main__":
    unittest.main()
    dbserver_message_size,
