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
    require("/bin/busybox telnetd -F -p 23 -l /bin/sh" in MODULE,
            "telnetd must stay in the owned foreground process and bypass login")
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
    require("169\\.254\\." in MODULE,
            "the status message must prefer the Link Export AutoIP address")


if __name__ == "__main__":
    main()
