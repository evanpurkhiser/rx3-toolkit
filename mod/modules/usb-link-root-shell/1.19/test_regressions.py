# SPDX-License-Identifier: MPL-2.0
from __future__ import annotations

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require(MANIFEST["default"] is False, "an unauthenticated root shell must be opt-in")
    require(set(MANIFEST["conflicts"]) == {"telnet", "usb-serial"},
            "the root shell must exclude the other interactive access modules")
    require("exec /bin/busybox telnetd -F -p 23 -l /bin/sh" in MODULE,
            "telnetd must stay in the owned foreground process and bypass login")
    require("cd /\n        exec /bin/busybox telnetd" in MODULE,
            "telnetd must not pin the decrypted ISO through its working directory")
    require("USB_LINK_ROOT_SHELL_PID=/tmp/" in MODULE,
            "the foreground listener needs a volatile ownership record")
    require("usb_link_root_shell_owned_listener" in MODULE,
            "reinsertion must recognize the module's existing listener")
    require('toupper(local[2]) == "0017"' in MODULE and
            'toupper($4) == "0A"' in MODULE,
            "port checks must require an IPv4 TCP listener on port 23")
    require('kill "$shell_pid"' in MODULE,
            "failed listener verification must stop the launched process")
    require("register_prepare_hook usb_link_root_shell_start" in MODULE,
            "the shell must start while the stock Link Export network is active")
    require("USB_LINK_ROOT_SHELL_INTERFACE=eth0" in MODULE and
            "USB_LINK_ROOT_SHELL_ALIAS=eth0:rx3shell" in MODULE,
            "the deterministic address must use a secondary alias on Link Export")
    require("USB_LINK_ROOT_SHELL_ADDRESS=169.254.100.2" in MODULE,
            "the documented shell address must remain stable")
    require("USB_LINK_ROOT_SHELL_PEER=169.254.100.1" in MODULE,
            "the server-side recovery address must remain stable")
    require('netmask 255.255.0.0 up' in MODULE,
            "the secondary address must cover the Link Export AutoIP subnet")
    require('route add -host "$USB_LINK_ROOT_SHELL_PEER"' in MODULE and
            'dev "$USB_LINK_ROOT_SHELL_INTERFACE"' in MODULE,
            "return traffic must stay on USB-B if usb0 also enters AutoIP")

    configure = MODULE.index("usb_link_root_shell_configure_address || return 1")
    listener = MODULE.index("if usb_link_root_shell_owned_listener", configure)
    launch = MODULE.index("/bin/busybox telnetd", listener)
    require(configure < listener < launch,
            "the address must be verified before reusing or launching telnetd")
    require("! usb_link_root_shell_address_ready" in MODULE,
            "the alias command must be followed by an address verification")


if __name__ == "__main__":
    main()
