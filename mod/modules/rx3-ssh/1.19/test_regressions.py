#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Security and packaging guards for authenticated RX3 SSH."""

from __future__ import annotations

import hashlib
import json
import pathlib
import subprocess
import unittest


HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MODULE = (HERE / "module.sh").read_text()
MANIFEST = json.loads((HERE / "manifest.json").read_text())
BUILD_TOOL = ROOT / "tools/rx3_dropbear"


class Rx3SshTests(unittest.TestCase):
    def test_module_is_opt_in_and_transport_independent(self) -> None:
        self.assertFalse(MANIFEST["default"])
        self.assertEqual(MANIFEST["requires"], [])
        self.assertNotIn("usb-link-root-shell", MANIFEST["conflicts"])
        self.assertNotIn("telnet", MANIFEST["conflicts"])
        self.assertEqual(MANIFEST["conflicts"], [])

    def test_packaged_binary_matches_reproducible_build(self) -> None:
        actual = hashlib.sha256((HERE / "dropbearmulti").read_bytes()).hexdigest()
        self.assertEqual(
            actual,
            "d0f9410f8dbcdb39198b7c7194626e56071ba13d2bd1b903b0a9a3b6de40f3f6",
        )

        header = subprocess.run(
            ["readelf", "-h", str(HERE / "dropbearmulti")],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("Machine:                           ARM", header)
        self.assertIn("Version5 EABI", header)
        self.assertIn("soft-float ABI", header)

        dynamic = subprocess.run(
            ["readelf", "-d", str(HERE / "dropbearmulti")],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("There is no dynamic section", dynamic)

        binary = HERE / "dropbearmulti"
        self.assertLess(binary.stat().st_size, 350_000)
        strings = subprocess.run(
            ["strings", str(binary)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout.splitlines()
        for algorithm in (
            "ssh-ed25519",
            "curve25519-sha256",
            "diffie-hellman-group14-sha256",
            "sntrup761x25519-sha512",
            "mlkem768x25519-sha256",
            "chacha20-poly1305@openssh.com",
            "aes128-ctr",
            "aes256-ctr",
        ):
            with self.subTest(algorithm=algorithm):
                self.assertIn(algorithm, strings)

    def test_build_disables_passwords_and_forwarding(self) -> None:
        options = (BUILD_TOOL / "localoptions.h").read_text()
        for setting in (
            "DROPBEAR_SVR_PASSWORD_AUTH",
            "DROPBEAR_SVR_PAM_AUTH",
            "DROPBEAR_SVR_LOCALTCPFWD",
            "DROPBEAR_SVR_REMOTETCPFWD",
            "DROPBEAR_SVR_LOCALSTREAMFWD",
            "DROPBEAR_SVR_REMOTESTREAMFWD",
            "DROPBEAR_SVR_AGENTFWD",
            "DROPBEAR_X11FWD",
        ):
            with self.subTest(setting=setting):
                self.assertIn(f"#define {setting} 0", options)
        self.assertIn("#define DROPBEAR_ED25519 1", options)
        self.assertIn("#define DROPBEAR_REEXEC 0", options)
        self.assertIn("#define DROPBEAR_SVR_DROP_PRIVS 0", options)
        self.assertNotIn("#define DROPBEAR_SVR_MULTIUSER 0", options)

        root_patch = (BUILD_TOOL / "rx3-static-root.patch").read_text()
        self.assertIn("ses.authstate.pw_uid == 0", root_patch)
        self.assertIn("ses.authstate.pw_gid == 0", root_patch)
        self.assertIn("return;", root_patch)

        build = (BUILD_TOOL / "build.sh").read_text()
        self.assertIn("musl-1.2.5.tar.gz", build)
        self.assertIn("--gc-sections", build)
        self.assertIn('LTM_CFLAGS="-Os ', build)

    def test_private_material_is_generated_at_runtime(self) -> None:
        packaged = {entry["target"] for entry in MANIFEST["files"]}
        self.assertEqual(packaged, {"module.sh", "dropbearmulti"})
        self.assertIn("RX3_SSH_CONFIG=$USB/RX3_SSH", MODULE)
        self.assertIn(
            "RX3_SSH_SOURCE=/mnt/iso/modules/rx3-ssh/dropbearmulti", MODULE
        )
        self.assertIn("RX3_SSH_AUTH_SOURCE=$RX3_SSH_CONFIG/authorized_keys", MODULE)
        self.assertIn(
            "RX3_SSH_HOST_KEY=$RX3_SSH_RUNTIME/dropbear_ed25519_host_key",
            MODULE,
        )
        self.assertIn('"$RX3_SSH_DROPBEARKEY" -t ed25519', MODULE)
        self.assertNotIn("PRIVATE KEY", MODULE)

    def test_executable_is_copied_to_ram_before_launch(self) -> None:
        self.assertIn("RX3_SSH_EXEC_RUNTIME=/dev/shm/rx3-ssh", MODULE)
        self.assertIn(
            "RX3_SSH_EXECUTABLE=$RX3_SSH_EXEC_RUNTIME/dropbearmulti", MODULE
        )
        self.assertIn("RX3_SSH_SOURCE_SHA1=", MODULE)
        self.assertIn('sha1sum "$RX3_SSH_SOURCE"', MODULE)
        self.assertIn('cp "$RX3_SSH_SOURCE" "$executable_temporary"', MODULE)
        self.assertIn('chmod 700 "$executable_temporary"', MODULE)
        self.assertIn('sha1sum "$executable_temporary"', MODULE)
        self.assertIn('mv -f "$executable_temporary" "$RX3_SSH_EXECUTABLE"', MODULE)
        self.assertIn('ln -sf "$RX3_SSH_EXECUTABLE" "$RX3_SSH_DROPBEAR"', MODULE)
        self.assertIn('ln -sf "$RX3_SSH_EXECUTABLE" "$RX3_SSH_DROPBEARKEY"', MODULE)
        self.assertNotIn('ln -sf "$RX3_SSH_SOURCE"', MODULE)

    def test_authorized_keys_are_required_and_installed_strictly(self) -> None:
        self.assertIn('$1 != "ssh-ed25519" { invalid = 1 }', MODULE)
        self.assertIn("chmod go-w /root", MODULE)
        self.assertIn('chmod 700 "$RX3_SSH_AUTH_DIRECTORY"', MODULE)
        self.assertIn('chown 0:0 "$auth_temporary"', MODULE)
        self.assertIn('chmod 600 "$auth_temporary"', MODULE)

    def test_daemon_uses_public_key_only_configuration(self) -> None:
        self.assertIn('RX3_SSH_PORT=22', MODULE)
        self.assertIn('exec "$RX3_SSH_DROPBEAR" -F -s -g -m', MODULE)
        self.assertIn("cd /\n        exec \"$RX3_SSH_DROPBEAR\"", MODULE)
        self.assertIn('-D "$RX3_SSH_AUTH_DIRECTORY"', MODULE)
        self.assertIn('-r "$RX3_SSH_HOST_KEY"', MODULE)
        self.assertIn('-p "0.0.0.0:$RX3_SSH_PORT"', MODULE)
        daemon_command = MODULE[MODULE.index('exec "$RX3_SSH_DROPBEAR" -F'):]
        daemon_command = daemon_command[: daemon_command.index(") > \"$RX3_SSH_LOG\"")]
        self.assertNotIn(" -w ", daemon_command)


if __name__ == "__main__":
    unittest.main()
