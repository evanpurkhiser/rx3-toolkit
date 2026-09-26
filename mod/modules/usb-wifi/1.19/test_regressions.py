#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regression guards for native USB Wi-Fi bring-up."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
MODULE = (HERE / "module.sh").read_text()
MANIFEST = json.loads((HERE / "manifest.json").read_text())


class UsbWifiTests(unittest.TestCase):
    def test_manifest_is_isolated_from_s3_networking(self) -> None:
        self.assertEqual(MANIFEST["id"], "usb-wifi")
        self.assertFalse(MANIFEST["default"])
        self.assertEqual(MANIFEST["conflicts"], [])
        self.assertEqual(MANIFEST["requires"], ["core"])
        self.assertNotIn("build_files", MANIFEST)

    def test_driver_order_and_required_payloads(self) -> None:
        targets = [item["target"] for item in MANIFEST["files"]]
        modules = [
            "compat-average.ko",
            "cfg80211.ko",
            "mac80211.ko",
            "rtlwifi.ko",
            "rtl8192c-common.ko",
            "rtl8192cu.ko",
        ]
        self.assertEqual([target for target in targets if target.endswith(".ko")], modules)
        self.assertIn("rtlwifi/rtl8192cufw.bin", targets)
        self.assertIn("wpa_supplicant", targets)
        self.assertIn("wpa_cli", targets)
        self.assertIn("wpa_supplicant.conf.example", targets)
        self.assertIn("rx3-ifrename", targets)
        self.assertEqual(
            {target for target in targets if target.startswith("licenses/")},
            {
                "licenses/COPYING.linux",
                "licenses/COPYING.libnl",
                "licenses/COPYING.wpa_supplicant",
                "licenses/COPYRIGHT.musl",
            },
        )

    def test_packaged_realtek_firmware_is_pinned(self) -> None:
        firmware = HERE / "rtlwifi/rtl8192cufw.fw"
        self.assertEqual(
            hashlib.sha256(firmware.read_bytes()).hexdigest(),
            "6327de71c544c27fe909d509a3d5605d46b94c102a3c27700f812b95cbe74254",
        )

    def test_packaged_kernel_modules_match_verified_build(self) -> None:
        expected = {
            "compat-average.ko": "b18d5ca956b5282efe1d638e58ee92c465cb825cdef22a75dce25330c2128e5d",
            "cfg80211.ko": "7466fee71a1287a9e687f6f1fe9a7afbf0fe13f82196bae770f4d9a3f8395077",
            "mac80211.ko": "14759caf3b7cba52bfa63825dda64793f42280e50ba034a512ea0c3da05127dc",
            "rtlwifi.ko": "c5de16fcad763671b0a034ced9bac6f033abce6dd0f3a81ebe2a34cccb21613c",
            "rtl8192c-common.ko": "e98a8af6647a0b80b35d259476a457ab21a84aa38f746e9a52ec92727d04d986",
            "rtl8192cu.ko": "85bcdf6591ee9d98e8921e518810b2295cc77d340bbb0161146d7a1f4a0e81e9",
        }
        for filename, digest in expected.items():
            with self.subTest(filename=filename):
                self.assertEqual(
                    hashlib.sha256((HERE / filename).read_bytes()).hexdigest(), digest
                )

    def test_packaged_userspace_matches_verified_build(self) -> None:
        expected = {
            "wpa_supplicant": "5e0ef9caf7583cd73e3a708e535ad7b52fbfc1a1cf56bff31e85d14436959d72",
            "wpa_cli": "82648df923d17f2e9dfb434dddf330deb7748a0d87b86b4a6bd1d7d992fb2737",
            "rx3-ifrename": "4c7c0d35a481e5c7597381a6a1df093a07c3a5051433e753b13def3fee46f84e",
        }
        for filename, digest in expected.items():
            with self.subTest(filename=filename):
                self.assertEqual(
                    hashlib.sha256((HERE / filename).read_bytes()).hexdigest(), digest
                )

    def test_stock_interface_name_moves_to_wifi(self) -> None:
        self.assertNotIn("register_patch", MODULE)
        self.assertNotIn("link_bootstrap", MODULE)
        self.assertNotIn("register_runtime_preload", MODULE)
        self.assertNotIn("librx3_link_bootstrap", str(MANIFEST))
        self.assertIn("USB_WIFI_APPLICATION_INTERFACE=eth0", MODULE)
        self.assertIn("USB_WIFI_REAR_INTERFACE=usbB0", MODULE)
        self.assertIn("register_stopped_hook usb_wifi_swap_interfaces", MODULE)
        self.assertIn(
            '"$USB_WIFI_IFRENAME" "$USB_WIFI_APPLICATION_INTERFACE"', MODULE
        )
        self.assertIn(
            '"$USB_WIFI_IFRENAME" "$USB_WIFI_SOURCE_INTERFACE"', MODULE
        )
        self.assertIn(
            'udhcpc -i "$USB_WIFI_INTERFACE" -T 2 -t 3 -n -q',
            MODULE,
        )
        self.assertIn("usb_wifi_dhcp_running", MODULE)
        self.assertIn("USB_WIFI_DHCP_STATE=rbp-bound", MODULE)
        self.assertIn("USB_WIFI_DHCP_STATE=recovery-bound", MODULE)

    def test_state_records_network_outcome(self) -> None:
        self.assertIn('echo "application_interface=$USB_WIFI_APPLICATION_INTERFACE"', MODULE)
        self.assertIn('echo "rear_interface=$USB_WIFI_REAR_INTERFACE"', MODULE)
        self.assertIn('echo "dhcp=$USB_WIFI_DHCP_STATE"', MODULE)
        self.assertIn('echo "ipv4_address=${USB_WIFI_ADDRESS:-none}"', MODULE)

    def test_interface_swap_registers_compensating_rollback(self) -> None:
        self.assertIn("register_rollback_hook usb_wifi_rollback_swap", MODULE)
        self.assertIn('USB_WIFI_SWAP_CHANGED=0', MODULE)
        self.assertIn('[ "$USB_WIFI_SWAP_CHANGED" = "1" ] || return 0', MODULE)
        self.assertIn("usb_wifi_release_application_name", MODULE)
        self.assertIn("usb_wifi_unbind_adapter", MODULE)
        self.assertLess(
            MODULE.index('USB_WIFI_SWAP_CHANGED=1', MODULE.index("usb_wifi_swap_interfaces")),
            MODULE.index(
                '"$USB_WIFI_IFRENAME" "$USB_WIFI_SOURCE_INTERFACE"',
                MODULE.index("usb_wifi_swap_interfaces"),
            ),
        )

    def test_rollback_restores_rear_eth0_when_wifi_disappears(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            net = root / "net"
            runtime.mkdir()
            (net / "usbB0").mkdir(parents=True)
            ifrename = runtime / "rx3-ifrename"
            ifrename.write_text('#!/bin/sh\nmv "$NET_ROOT/$1" "$NET_ROOT/$2"\n')
            ifrename.chmod(0o755)
            ifconfig = root / "ifconfig"
            ifconfig.write_text("#!/bin/sh\nexit 0\n")
            ifconfig.chmod(0o755)

            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_runtime_preload() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                f"USB_WIFI_RUNTIME_DIRECTORY='{runtime}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "USB_WIFI_SOURCE_INTERFACE=wlan0\n"
                "USB_WIFI_INTERFACE=eth0\n"
                "USB_WIFI_SWAP_CHANGED=1\n"
                "usb_wifi_rollback_swap\n"
                f"test -d '{net / 'eth0'}'\n"
                f"test ! -e '{net / 'usbB0'}'\n"
            )
            environment = os.environ | {
                "NET_ROOT": str(net),
                "PATH": f"{root}:{os.environ['PATH']}",
            }
            subprocess.run(
                ["sh", str(harness)], check=True, capture_output=True, env=environment
            )

    def test_rollback_unbinds_wifi_when_reverse_rename_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            net = root / "net"
            runtime.mkdir()
            (net / "eth0").mkdir(parents=True)
            (net / "usbB0").mkdir()
            ifrename = runtime / "rx3-ifrename"
            ifrename.write_text(
                '#!/bin/sh\n[ "$1" != eth0 ] || exit 1\n'
                'mv "$NET_ROOT/$1" "$NET_ROOT/$2"\n'
            )
            ifrename.chmod(0o755)
            ifconfig = root / "ifconfig"
            ifconfig.write_text("#!/bin/sh\nexit 0\n")
            ifconfig.chmod(0o755)

            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_runtime_preload() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                "sleep() { :; }\n"
                f"USB_WIFI_RUNTIME_DIRECTORY='{runtime}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "USB_WIFI_SOURCE_INTERFACE=wlan0\n"
                "USB_WIFI_INTERFACE=eth0\n"
                "USB_WIFI_SWAP_CHANGED=1\n"
                f"usb_wifi_unbind_adapter() {{ rm -rf '{net / 'eth0'}'; }}\n"
                "usb_wifi_rollback_swap\n"
                f"test -d '{net / 'eth0'}'\n"
                f"test ! -e '{net / 'usbB0'}'\n"
            )
            environment = os.environ | {
                "NET_ROOT": str(net),
                "PATH": f"{root}:{os.environ['PATH']}",
            }
            subprocess.run(
                ["sh", str(harness)], check=True, capture_output=True, env=environment
            )

    def test_credentials_are_copied_to_protected_ram(self) -> None:
        self.assertIn('$USB/RX3_WIFI/wpa_supplicant.conf', MODULE)
        self.assertIn('cp "$USB_WIFI_CONFIG" "$USB_WIFI_RUNTIME_CONFIG"', MODULE)
        self.assertIn('chmod 600 "$USB_WIFI_RUNTIME_CONFIG"', MODULE)
        self.assertNotIn("cat \"$USB_WIFI_CONFIG\"", MODULE)
        self.assertNotIn("psk=", MODULE)
        self.assertNotIn('-f"$USB_WIFI_LOG"', MODULE)

    def test_failed_initial_association_stops_owned_supplicant(self) -> None:
        prepare = MODULE[MODULE.index("usb_wifi_prepare()") :]
        self.assertIn("usb_wifi_wait_for_association || {", prepare)
        self.assertIn("usb_wifi_stop_owned_supplicant", prepare)

    def test_exact_adapter_is_selected_from_usb_topology(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            usb = root / "usb"
            net = root / "net"
            device = usb / "1-2"
            interface = usb / "1-2:1.0" / "net" / "wlan0"
            interface.mkdir(parents=True)
            (net / "wlan0").mkdir(parents=True)
            device.mkdir(parents=True)
            (device / "idVendor").write_text("7392\n")
            (device / "idProduct").write_text("7811\n")

            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_patch() { :; }\n"
                "register_runtime_preload() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                f"USB_WIFI_USB_SYSFS='{usb}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "usb_wifi_detect_interface\n"
                'printf "%s %s\\n" "$USB_WIFI_SOURCE_INTERFACE" '
                '"$USB_WIFI_TOPOLOGY"\n'
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, "wlan0 1-2\n")

    def test_lan_address_ignores_link_local_fallback(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture = root / "ifconfig.txt"
            fixture.write_text(
                "usb0      Link encap:Ethernet  HWaddr 74:da:38:2b:2d:45\n"
                "          inet addr:169.254.41.8  Bcast:169.254.255.255\n"
                "          inet addr:10.0.0.144  Bcast:10.0.0.255\n"
            )
            ifconfig = root / "ifconfig"
            ifconfig.write_text('#!/bin/sh\ncat "$IFCONFIG_FIXTURE"\n')
            ifconfig.chmod(0o755)

            harness = root / "harness.sh"
            harness.write_text(
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_patch() { :; }\n"
                "register_runtime_preload() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                f". '{HERE / 'module.sh'}'\n"
                "usb_wifi_lan_address\n"
            )
            environment = os.environ | {
                "IFCONFIG_FIXTURE": str(fixture),
                "PATH": f"{root}:{os.environ['PATH']}",
            }
            result = subprocess.run(
                ["sh", str(harness)],
                check=True,
                text=True,
                capture_output=True,
                env=environment,
            )
            self.assertEqual(result.stdout, "10.0.0.144\n")


if __name__ == "__main__":
    unittest.main()
