import socket
import unittest

from tools.rx3_link_export.relay import PRO_DJ_LINK_MAGIC, packet_type, translate_addresses


class RelayTests(unittest.TestCase):
    def test_translates_every_embedded_address(self):
        source = "10.0.0.119"
        replacement = "169.254.100.1"
        address = socket.inet_aton(source)
        packet = b"prefix" + address + b"middle" + address + b"suffix"

        translated = translate_addresses(packet, source, replacement)

        self.assertNotIn(address, translated)
        self.assertEqual(translated.count(socket.inet_aton(replacement)), 2)

    def test_reports_pro_dj_link_packet_type(self):
        self.assertEqual(packet_type(PRO_DJ_LINK_MAGIC + b"\x11payload"), "0x11")

    def test_rejects_other_payloads(self):
        self.assertEqual(packet_type(b"not pro dj link"), "unknown")


if __name__ == "__main__":
    unittest.main()
