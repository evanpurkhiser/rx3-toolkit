#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regression guards for S3 CDC-NCM bring-up and rbp redirection."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import tempfile
import unittest


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MODULE = HERE / "module.sh"


class S3LinkBridgeTests(unittest.TestCase):
    def test_packaged_modules_match_reproducible_build_outputs(self) -> None:
        source = ROOT / "tools/rx3_usbnet_kernel"
        for name in ("usbnet.ko", "cdc_ncm.ko"):
            with self.subTest(name=name):
                expected = hashlib.sha256((source / name).read_bytes()).digest()
                actual = hashlib.sha256((HERE / name).read_bytes()).digest()
                self.assertEqual(actual, expected)

        expected_status = hashlib.sha256(
            (ROOT / "firmware/esp32-s3-link/tools/ncm-status/build/rx3-ncm-status").read_bytes()
        ).digest()
        actual_status = hashlib.sha256((HERE / "rx3-ncm-status").read_bytes()).digest()
        self.assertEqual(actual_status, expected_status)

    def test_manifest_packages_framework_before_class_driver(self) -> None:
        manifest = json.loads((HERE / "manifest.json").read_text())
        targets = [item["target"] for item in manifest["files"]]
        self.assertLess(targets.index("usbnet.ko"), targets.index("cdc_ncm.ko"))
        self.assertIn("rx3-ncm-status", targets)

    def test_guarded_patches_redirect_networking_and_dhcp(self) -> None:
        script = MODULE.read_text()
        self.assertIn(
            "register_patch 4155000 '\\145\\164\\150\\060' "
            "'\\165\\163\\142\\060' network-interface-eth0-to-usb0",
            script,
        )
        self.assertIn(
            "register_patch 5084504 '\\151\\040\\145\\164' "
            "'\\151\\040\\165\\163' dhcp-interface-prefix-eth0-to-usb0",
            script,
        )
        self.assertIn(
            "register_patch 5084508 '\\150\\060\\040\\055' "
            "'\\142\\060\\040\\055' dhcp-interface-suffix-eth0-to-usb0",
            script,
        )
        offsets = [4155000, 5084504, 5084508]
        self.assertTrue(all(offset % 4 == 0 for offset in offsets))
        self.assertIn("S3_LINK_BRIDGE_APPLICATION_INTERFACE=usb0", script)
        self.assertIn("S3_LINK_BRIDGE_REAR_INTERFACE=eth0", script)
        self.assertNotIn("ifrename", script)

    def test_management_address_is_an_alias(self) -> None:
        script = MODULE.read_text()
        self.assertIn('S3_LINK_BRIDGE_MANAGEMENT_ADDRESS:=172.31.254.2', script)
        self.assertIn('S3_LINK_BRIDGE_MANAGEMENT_NETMASK:=255.255.255.252', script)
        self.assertIn('ifconfig "$S3_LINK_BRIDGE_INTERFACE:mgmt"', script)
        self.assertIn("rbp owns DHCP", script)

    def test_probe_failure_aborts_before_the_patch_is_written(self) -> None:
        script = MODULE.read_text()
        prepare = script[script.index("s3_link_bridge_prepare()") :]
        self.assertIn("s3_link_bridge_probe || {", prepare)
        self.assertIn("refusing to redirect rbp from rear USB", prepare)
        self.assertIn("return 1", prepare)

    def test_usb_identity_selects_usb0_below_the_matching_device(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            usb = root / "usb"
            net = root / "net"
            device = usb / "1-2"
            interface = usb / "1-2:1.0" / "net" / "usb0"
            interface.mkdir(parents=True)
            (net / "usb0").mkdir(parents=True)
            device.mkdir(parents=True)
            (device / "idVendor").write_text("303a\n")
            (device / "idProduct").write_text("4012\n")
            (device / "product").write_text("RX3 Wi-Fi Link Bridge\n")

            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "module_begin() { :; }\n"
                "register_patch() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "say() { :; }\n"
                f"S3_LINK_BRIDGE_USB_SYSFS='{usb}'\n"
                f"S3_LINK_BRIDGE_NET_SYSFS='{net}'\n"
                f". '{MODULE}'\n"
                "s3_link_bridge_detect_interface\n"
                'printf "%s %s\\n" "$S3_LINK_BRIDGE_INTERFACE" '
                '"$S3_LINK_BRIDGE_TOPOLOGY"\n'
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, "usb0 1-2\n")


if __name__ == "__main__":
    unittest.main()
