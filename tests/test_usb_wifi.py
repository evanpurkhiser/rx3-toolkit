#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Regression guards for native USB Wi-Fi bring-up."""

from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HERE = ROOT / "mod/modules/usb-wifi"
MODULE = (HERE / "module.sh").read_text()
MANIFEST = json.loads((HERE / "manifest.json").read_text())
PROFILE = HERE / "profiles/edimax-ew-7811un-v1"
T3U_PROFILE = HERE / "profiles/tp-link-archer-t3u-v1"


class UsbWifiTests(unittest.TestCase):
    def test_profiles_define_valid_usb_wifi_runtime_data(self) -> None:
        for directory in sorted((HERE / "profiles").iterdir()):
            if not directory.is_dir():
                continue

            profile = json.loads((directory / "profile.json").read_text())
            self.assertEqual(profile["id"], directory.name)
            self.assertIsInstance(profile["name"], str)
            self.assertTrue(profile["name"].strip())
            self.assertIsInstance(profile["chipset"], str)
            self.assertTrue(profile["chipset"].strip())
            self.assertTrue(profile["usb_ids"])
            self.assertTrue(all(
                re.fullmatch(r"[0-9a-fA-F]{4}:[0-9a-fA-F]{4}", usb_id)
                for usb_id in profile["usb_ids"]
            ))

            ids = [
                line.strip().lower()
                for line in (directory / "usb.ids").read_text().splitlines()
                if line.strip() and not line.lstrip().startswith("#")
            ]
            self.assertEqual(
                ids, [usb_id.lower() for usb_id in profile["usb_ids"]]
            )
            driver = (directory / "supplicant.driver").read_text().strip()
            self.assertEqual(driver, profile["supplicant_driver"])
            self.assertRegex(driver, r"^[A-Za-z0-9_-]+$")

            for name, allow_empty in (
                ("modules.load", False),
                ("firmware.list", True),
            ):
                rows = [
                    line.split()
                    for line in (directory / name).read_text().splitlines()
                    if line.strip() and not line.lstrip().startswith("#")
                ]
                self.assertTrue(allow_empty or rows)
                self.assertTrue(all(len(row) == 2 for row in rows))
                self.assertTrue(all(
                    not value.startswith("/") and ".." not in Path(value).parts
                    for row in rows for value in row
                ))

    def test_manifest_is_isolated_from_s3_networking(self) -> None:
        self.assertEqual(MANIFEST["id"], "usb-wifi")
        self.assertFalse(MANIFEST["default"])
        self.assertEqual(MANIFEST["firmwares"], ["1.19"])
        self.assertEqual(MANIFEST["category"], "diagnostics")
        self.assertTrue(MANIFEST["advanced"])
        self.assertTrue(MANIFEST["profile_required"])
        self.assertEqual(MANIFEST["conflicts"], [])
        self.assertEqual(MANIFEST["requires"], [])
        self.assertNotIn("build_files", MANIFEST)
        self.assertNotIn("CORE_OBJECT", MODULE)

    def test_driver_order_and_required_payloads(self) -> None:
        targets = [item["target"] for item in MANIFEST["files"]]
        modules = [line.split()[1] for line in (PROFILE / "modules.load").read_text().splitlines()]
        self.assertEqual(
            modules,
            [
                "compat-average.ko", "cfg80211.ko", "mac80211.ko",
                "rtlwifi.ko", "rtl8192c-common.ko", "rtl8192cu.ko",
            ],
        )
        self.assertNotIn("7392", MODULE)
        self.assertNotIn("rtl8192", MODULE)
        self.assertIn('done < "$USB_WIFI_MODULES"', MODULE)
        self.assertIn('done < "$USB_WIFI_FIRMWARE"', MODULE)
        self.assertEqual(
            (PROFILE / "firmware.list").read_text().strip(),
            "rtlwifi/rtl8192cufw.bin rtlwifi/rtl8192cufw.bin",
        )
        self.assertIn("hardware", targets)
        self.assertIn("wpa_supplicant.conf.example", targets)
        self.assertEqual(
            {target for target in targets if target.startswith("licenses/")},
            {
                "licenses/COPYING.linux",
                "licenses/COPYING.libnl",
                "licenses/COPYING.wpa_supplicant",
                "licenses/COPYRIGHT.musl",
            },
        )

    def test_opaque_payloads_are_generated_artifacts(self) -> None:
        generated = {
            item["target"]
            for item in MANIFEST["files"]
            if item.get("artifact") is True
        }
        self.assertEqual(generated, {"hardware"})
        hardware = next(
            item for item in MANIFEST["files"] if item.get("artifact") is True
        )
        self.assertTrue(hardware["directory"])
        self.assertEqual(hardware["source"], ".")

    def test_third_party_artifact_inputs_are_pinned(self) -> None:
        userspace = ROOT / "tools/rx3_usb_wifi_userspace"
        fetch = (PROFILE / "fetch-sources.sh").read_text()
        checksums = (PROFILE / "sources.sha256").read_text()
        self.assertIn("linux-firmware/-/raw/main/rtlwifi/rtl8192cufw.bin", fetch)
        self.assertIn("LICENSES/LICENCE.rtlwifi_firmware.txt", fetch)
        self.assertIn(
            "6327de71c544c27fe909d509a3d5605d46b94c102a3c27700f812b95cbe74254",
            checksums,
        )
        self.assertIn(
            "a61351665b4f264f6c631364f85b907d8f8f41f8b369533ef4021765f9f3b62e",
            checksums,
        )

    def test_archer_t3u_profile_is_exact_and_firmwareless(self) -> None:
        profile = json.loads((T3U_PROFILE / "profile.json").read_text())
        self.assertEqual(profile["chipset"], "Realtek RTL8812BU")
        self.assertEqual(profile["usb_ids"], ["2357:012d"])
        self.assertEqual(
            (T3U_PROFILE / "modules.load").read_text().splitlines(),
            [
                "compat_average compat-average.ko",
                "cfg80211 cfg80211.ko",
                "88x2bu 88x2bu.ko",
            ],
        )
        self.assertEqual((T3U_PROFILE / "firmware.list").read_text(), "")
        self.assertEqual((T3U_PROFILE / "artifacts.list").read_text(), "")

        kernel_profile = (
            ROOT / "tools/rx3_usb_wifi_kernel/profiles/tp-link-archer-t3u-v1"
        )
        symbols = (kernel_profile / "production.symvers").read_text()
        self.assertIn(
            "0x1f07fbb5\t__raw_spin_lock_init\tvmlinux\tEXPORT_SYMBOL",
            symbols,
        )
        self.assertIn(
            "0xb17bc4cf\tmodule_layout\tvmlinux\tEXPORT_SYMBOL",
            symbols,
        )
        sources = (kernel_profile / "sources.sha256").read_text()
        self.assertIn(
            "a07d60f4d79cd8ae7c5ba2f724ee72ce998218b5603709373e8585e5a3e01639",
            sources,
        )

    def test_ci_builds_artifacts_before_desktop_packages(self) -> None:
        workflow = (ROOT / ".github/workflows/ci.yml").read_text()
        self.assertIn("usb-wifi-artifacts:", workflow)
        self.assertIn("make kernel-source FIRMWARE=1.19", workflow)
        self.assertIn("PROFILE=edimax-ew-7811un-v1", workflow)
        self.assertIn("PROFILE=tp-link-archer-t3u-v1", workflow)
        self.assertIn("name: runtime-assets-usb-wifi", workflow)
        self.assertIn("- usb-wifi-artifacts", workflow)

    def test_stock_interface_name_moves_to_wifi(self) -> None:
        self.assertNotIn("register_patch", MODULE)
        self.assertNotIn("link_bootstrap", MODULE)
        self.assertNotIn("register_runtime_preload", MODULE)
        self.assertNotIn("librx3_link_bootstrap", str(MANIFEST))
        self.assertIn("USB_WIFI_APPLICATION_INTERFACE=eth0", MODULE)
        self.assertIn("USB_WIFI_REAR_INTERFACE=usb0", MODULE)
        self.assertIn("USB_WIFI_REAR_ADDRESS=169.254.100.2", MODULE)
        self.assertIn("usb_wifi_configure_rear_interface", MODULE)
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
        self.assertIn('echo "wifi_connected=$wifi_connected"', MODULE)
        self.assertIn('echo "wifi_ssid=$wifi_ssid"', MODULE)
        self.assertIn('echo "wifi_address=$USB_WIFI_ADDRESS"', MODULE)
        self.assertIn('echo "dhcp=$USB_WIFI_DHCP_STATE"', MODULE)
        self.assertIn('echo "ipv4_address=${USB_WIFI_ADDRESS:-none}"', MODULE)

    def test_utility_menu_reports_association_and_address(self) -> None:
        self.assertIn(
            'register_menu_item RX3-TOOLKIT "WIFI CONNECTED" field', MODULE
        )
        self.assertIn(
            '"$USB_WIFI_STATE" wifi_connected', MODULE
        )
        self.assertIn(
            'register_menu_item RX3-TOOLKIT "WIFI SSID" field', MODULE
        )
        self.assertIn('"$USB_WIFI_STATE" wifi_ssid', MODULE)
        self.assertIn(
            'register_menu_item RX3-TOOLKIT "WIFI ADDRESS" field', MODULE
        )
        self.assertIn('"$USB_WIFI_STATE" wifi_address', MODULE)
        self.assertIn('[ "$(usb_wifi_wpa_state)" = "COMPLETED" ]', MODULE)
        self.assertIn('mv -f "$USB_WIFI_STATE.tmp" "$USB_WIFI_STATE"', MODULE)

    def test_menu_state_follows_the_current_association(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            state = root / "state"
            interface = root / "net/eth0"
            interface.mkdir(parents=True)
            (interface / "carrier").write_text("1\n")
            cli = root / "wpa_cli"
            cli.write_text(
                "#!/bin/sh\n"
                "printf 'wpa_state=%s\\nssid=%s\\n' \"$TEST_WPA_STATE\" \"$TEST_SSID\"\n"
            )
            cli.chmod(0o755)
            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                f"USB_WIFI_STATE='{state}'\n"
                f"USB_WIFI_NET_SYSFS='{root / 'net'}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "USB_WIFI_INTERFACE=eth0\n"
                f"USB_WIFI_CLI='{cli}'\n"
                "USB_WIFI_ADDRESS=10.0.0.144\n"
                "TEST_WPA_STATE=COMPLETED\n"
                "TEST_SSID='Studio=5G'\n"
                "export TEST_WPA_STATE TEST_SSID\n"
                "ifconfig() { :; }\n"
                "usb_wifi_write_state\n"
                f"grep -qx 'wifi_connected=YES' '{state}'\n"
                f"grep -qx 'wifi_ssid=Studio=5G' '{state}'\n"
                f"grep -qx 'wifi_address=10.0.0.144' '{state}'\n"
                "USB_WIFI_ADDRESS=\n"
                "TEST_WPA_STATE=DISCONNECTED\n"
                "TEST_SSID=\n"
                "usb_wifi_write_state\n"
                f"grep -qx 'wifi_connected=NO' '{state}'\n"
                f"grep -qx 'wifi_ssid=' '{state}'\n"
                f"grep -qx 'wifi_address=' '{state}'\n"
            )
            subprocess.run(["sh", str(harness)], check=True)

    def test_diagnostic_log_uses_runtime_contract_path(self) -> None:
        self.assertIn(': "${USB_WIFI_LOG:=/tmp/rx3-usb-wifi.log}"', MODULE)
        self.assertNotIn(
            "USB_WIFI_LOG=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.log",
            MODULE,
        )

    def test_staged_config_uses_the_module_control_socket(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            runtime.mkdir()
            config = root / "wpa_supplicant.conf"
            config.write_text(
                "ctrl_interface=/dev/shm/stale-test/control\n"
                "network={\n    ssid=\"example\"\n}\n"
            )
            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                f"USB_WIFI_CONFIG='{config}'\n"
                f"USB_WIFI_RUNTIME_DIRECTORY='{runtime}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "usb_wifi_stage_config\n"
                'grep "^ctrl_interface=" "$USB_WIFI_RUNTIME_CONFIG"\n'
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, f"ctrl_interface={runtime}/control\n")

    def test_adapter_reset_cycles_the_recorded_usb_topology(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            driver = root / "driver"
            net = root / "net"
            usb = root / "usb"
            driver.mkdir()
            (driver / "bind").touch()
            (driver / "unbind").touch()
            (net / "wlan0").mkdir(parents=True)
            (usb / "2-1.2:1.0" / "net" / "wlan0").mkdir(parents=True)
            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "sleep() { :; }\n"
                f"USB_WIFI_USB_DRIVER='{driver}'\n"
                f"USB_WIFI_USB_SYSFS='{usb}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "USB_WIFI_TOPOLOGY=2-1.2\n"
                "USB_WIFI_SOURCE_INTERFACE=eth0\n"
                "usb_wifi_stop_owned_supplicant() { :; }\n"
                "usb_wifi_reset_adapter\n"
                'printf "%s\\n" "$USB_WIFI_SOURCE_INTERFACE"\n'
                f"cat '{driver / 'unbind'}'\n"
                f"cat '{driver / 'bind'}'\n"
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, "wlan0\n2-1.22-1.2")

    def test_existing_swapped_connection_is_reused(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            net = root / "net"
            runtime.mkdir()
            (runtime / "control").mkdir()
            (net / "eth0").mkdir(parents=True)
            (net / "usb0").mkdir()
            (net / "eth0" / "carrier").write_text("1\n")
            cli = runtime / "wpa_cli"
            cli.write_text("#!/bin/sh\necho wpa_state=COMPLETED\n")
            cli.chmod(0o755)
            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                f"USB_WIFI_RUNTIME_DIRECTORY='{runtime}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "USB_WIFI_SOURCE_INTERFACE=eth0\n"
                "usb_wifi_existing_association\n"
                'printf "%s\\n" "$USB_WIFI_INTERFACE"\n'
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, "eth0\n")

    def test_rear_usb_gets_a_stable_management_address(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            net = root / "net"
            (net / "usb0").mkdir(parents=True)
            calls = root / "ifconfig.calls"
            ifconfig = root / "ifconfig"
            ifconfig.write_text(
                "#!/bin/sh\n"
                'if [ "$#" -eq 1 ]; then\n'
                '  echo "usb0  inet addr:169.254.100.2"\n'
                "  exit 0\n"
                "fi\n"
                'printf "%s\\n" "$*" > "$IFCONFIG_CALLS"\n'
            )
            ifconfig.chmod(0o755)
            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                "usb_wifi_configure_rear_interface\n"
            )
            environment = os.environ | {
                "IFCONFIG_CALLS": str(calls),
                "PATH": f"{root}:{os.environ['PATH']}",
            }
            subprocess.run(
                ["sh", str(harness)], check=True, capture_output=True, env=environment
            )
            self.assertEqual(
                calls.read_text(), "usb0 169.254.100.2 netmask 255.255.0.0 up\n"
            )
            self.assertNotIn('udhcpc -i "$USB_WIFI_REAR_INTERFACE"', MODULE)

    def test_interface_swap_registers_compensating_rollback(self) -> None:
        self.assertIn("register_rollback_hook usb_wifi_rollback_swap", MODULE)
        self.assertIn('USB_WIFI_SWAP_CHANGED=0', MODULE)
        self.assertIn('[ "$USB_WIFI_SWAP_CHANGED" = "1" ] || return 0', MODULE)
        self.assertIn("usb_wifi_release_application_name", MODULE)
        self.assertIn("usb_wifi_unbind_adapter", MODULE)
        changed = MODULE.index(
            'USB_WIFI_SWAP_CHANGED=1', MODULE.index("usb_wifi_swap_interfaces")
        )
        self.assertLess(
            changed,
            MODULE.index(
                '"$USB_WIFI_IFRENAME" "$USB_WIFI_SOURCE_INTERFACE"', changed
            ),
        )

    def test_partial_swapped_topology_recovers_wifi_name(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            net = root / "net"
            runtime.mkdir()
            (net / "wlan0").mkdir(parents=True)
            (net / "usb0").mkdir()
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
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
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
                "USB_WIFI_INTERFACE=wlan0\n"
                "usb_wifi_stop_owned_supplicant() { :; }\n"
                "usb_wifi_start_supplicant() { :; }\n"
                "usb_wifi_wait_for_association() { :; }\n"
                "usb_wifi_swap_interfaces\n"
                f"test -d '{net / 'eth0'}'\n"
                f"test ! -e '{net / 'wlan0'}'\n"
                f"test -d '{net / 'usb0'}'\n"
            )
            environment = os.environ | {
                "NET_ROOT": str(net),
                "PATH": f"{root}:{os.environ['PATH']}",
            }
            subprocess.run(
                ["sh", str(harness)], check=True, capture_output=True, env=environment
            )

    def test_rollback_restores_rear_eth0_when_wifi_disappears(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            net = root / "net"
            runtime.mkdir()
            (net / "usb0").mkdir(parents=True)
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
                "register_menu_item() { :; }\n"
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
                f"test ! -e '{net / 'usb0'}'\n"
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
            (net / "usb0").mkdir()
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
                "register_menu_item() { :; }\n"
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
                f"test ! -e '{net / 'usb0'}'\n"
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
        associate = MODULE[
            MODULE.index("usb_wifi_associate()") : MODULE.index("usb_wifi_wpa_state()")
        ]
        self.assertIn("usb_wifi_stop_owned_supplicant", associate)

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
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                "say() { :; }\n"
                f"USB_WIFI_USB_SYSFS='{usb}'\n"
                f"USB_WIFI_NET_SYSFS='{net}'\n"
                f". '{HERE / 'module.sh'}'\n"
                f"USB_WIFI_IDS='{PROFILE / 'usb.ids'}'\n"
                "USB_WIFI_PROFILE_ID=edimax-ew-7811un-v1\n"
                "usb_wifi_detect_interface\n"
                'printf "%s %s\\n" "$USB_WIFI_SOURCE_INTERFACE" '
                '"$USB_WIFI_TOPOLOGY"\n'
            )
            result = subprocess.run(
                ["sh", str(harness)], check=True, text=True, capture_output=True
            )
            self.assertEqual(result.stdout, "wlan0 1-2\n")

    def test_profile_firmware_is_installed_at_its_declared_path(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            hardware = root / "hardware"
            source = hardware / "rtlwifi/rtl8192cufw.bin"
            source.parent.mkdir(parents=True)
            source.write_bytes(b"firmware")
            firmware_list = hardware / "firmware.list"
            firmware_list.write_text(
                "rtlwifi/rtl8192cufw.bin rtlwifi/rtl8192cufw.bin\n"
            )
            destination = root / "lib/firmware"

            harness = root / "harness.sh"
            harness.write_text(
                "set -eu\n"
                "USB=/media/usb\n"
                "module_begin() { :; }\n"
                "register_diagnostic_file() { :; }\n"
                "register_menu_item() { :; }\n"
                "register_prepare_hook() { :; }\n"
                "register_after_launch_hook() { :; }\n"
                "register_stopped_hook() { :; }\n"
                "register_rollback_hook() { :; }\n"
                "register_report_hook() { :; }\n"
                f". '{HERE / 'module.sh'}'\n"
                f"USB_WIFI_HARDWARE_DIRECTORY='{hardware}'\n"
                f"USB_WIFI_FIRMWARE='{firmware_list}'\n"
                f"USB_WIFI_FIRMWARE_DIRECTORY='{destination}'\n"
                "usb_wifi_install_firmware\n"
            )
            subprocess.run(["sh", str(harness)], check=True)
            self.assertEqual(
                (destination / "rtlwifi/rtl8192cufw.bin").read_bytes(),
                b"firmware",
            )

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
                "register_menu_item() { :; }\n"
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
