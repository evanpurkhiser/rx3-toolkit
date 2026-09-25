import socket
import unittest

from tools.rx3_link_export.relay import (
    ACTIVATE_PC_CONTROL,
    INITIALIZE_PC_CONTROL,
    PRO_DJ_LINK_MAGIC,
    RelayConfig,
    advertised_mac,
    advertised_ip,
    packet_type,
    translate_addresses,
    translate_identity,
    translate_rekordbox_packet,
)


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
        usb_rekordbox_mac=bytes.fromhex("c83dfc16af9a"),
    )


class RelayTests(unittest.TestCase):
    def test_preserves_lan_rekordbox_identity_while_translating_endpoint(self):
        config = test_config(rekordbox_ip="10.0.0.119")
        source_mac = bytes.fromhex("1c57dc3900bb")
        announcement = bytearray(PRO_DJ_LINK_MAGIC + b"\x06" + bytes(43))
        announcement[36] = 0x29
        announcement[38:44] = source_mac
        announcement[44:48] = socket.inet_aton(config.rekordbox_ip)

        translated = translate_rekordbox_packet(
            bytes(announcement), config, source_mac
        )

        self.assertEqual(translated[36], 0x29)
        self.assertEqual(translated[38:44], config.usb_rekordbox_mac)
        self.assertEqual(
            translated[44:48], socket.inet_aton(config.usb_rekordbox_ip)
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

    def test_reports_pro_dj_link_packet_type(self):
        self.assertEqual(packet_type(PRO_DJ_LINK_MAGIC + b"\x11payload"), "0x11")

    def test_rejects_other_payloads(self):
        self.assertEqual(packet_type(b"not pro dj link"), "unknown")



if __name__ == "__main__":
    unittest.main()
