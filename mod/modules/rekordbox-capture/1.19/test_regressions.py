#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static safety guards for the one-shot firmware 1.19 capture module."""

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
SOURCE = (ROOT / "rx3_rekordbox_capture.c").read_text()
PCAP = (ROOT / "rx3_eth0_capture.c").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin rekordbox-capture rekordbox_capture" in MODULE,
    "the module identity must match its catalog entry",
)
require(
    "cf309238491e73cdbdc1f08a09f7a3177e079068" in MODULE
    and "cf309238491e73cdbdc1f08a09f7a3177e079068" in SOURCE,
    "both runtime and capture header must pin the verified rbp image",
)
require(
    set(MANIFEST["conflicts"])
    == {"usb-link-root-shell", "usb-serial", "usb-telemetry"},
    "capture must exclude network aliases and competing USB instrumentation",
)
require("register_patch" not in MODULE, "capture must not patch rbp on disk")
require(
    'export RX3_REKORDBOX_CAPTURE_PATH=$REKORDBOX_CAPTURE_JSON' in MODULE
    and 'getenv(CAPTURE_PATH_ENV)' in SOURCE,
    "the preload output must resolve to the writable toolkit partition",
)
require(
    "register_stopped_hook rekordbox_capture_start_ethernet" in MODULE,
    "Ethernet capture must begin before the replacement rbp starts",
)
require(
    'REKORDBOX_CAPTURE_RESIDENT=1' in MODULE
    and 'say "Rekordbox capture already active, existing files preserved"' in MODULE,
    "reinsertion in one boot must preserve the one-shot capture",
)
require(
    "ifconfig" not in MODULE and "169.254.100.2" not in MODULE,
    "capture must leave the stock Link interface configuration untouched",
)

for address, guard in (
    ("0x002eb864", "0x40, 0x00, 0x52, 0xe3, 0xf0, 0x41, 0x2d, 0xe9"),
    ("0x00029568", "0xf0, 0x41, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1"),
    ("0x002f0238", "0x22, 0x30, 0xd0, 0xe5, 0xf0, 0x45, 0x2d, 0xe9"),
):
    require(address in SOURCE and guard in SOURCE, f"verified hook {address} drifted")

for callback in ("hooked_hid_incoming", "hooked_hid_outgoing", "hooked_midi_incoming"):
    body = re.search(rf"static void {callback}\(.*?\n\}}", SOURCE, re.S)
    require(body is not None, f"{callback} is missing")
    require("queue_record(" in body.group(0), f"{callback} must enqueue")
    require("original_" in body.group(0), f"{callback} must preserve stock behavior")
    for forbidden in ("write(", "open(", "lseek(", "recv("):
        require(forbidden not in body.group(0), f"{callback} may not perform file I/O")

require(
    "MSG_DONTWAIT" in SOURCE and "dropped_records" in SOURCE,
    "rbp callbacks must drop under pressure instead of blocking",
)
require(
    "CAPTURE_LIMIT (64u * 1024u * 1024u)" in SOURCE
    and "(unsigned long)size + 2048u > CAPTURE_LIMIT" in SOURCE,
    "the USB event file must reserve a complete record before its hard cap",
)
require(
    "AF_PACKET 17" in PCAP and "ETH_P_ALL 0x0003" in PCAP
    and "SIOCGIFINDEX 0x8933" in PCAP,
    "the helper must bind a full raw packet socket to the requested interface",
)
require(
    "PCAP_LIMIT (128u * 1024u * 1024u)" in PCAP
    and "SNAPLEN 65535u" in PCAP
    and "(uint32_t)count, (uint32_t)count" in PCAP,
    "PCAP must retain complete frames and enforce its hard cap",
)
for forbidden in ("SYS_SEND", "SYS_SENDTO", "SIOCSIFADDR", "SIOCSIFFLAGS"):
    require(forbidden not in PCAP, "the Ethernet helper must remain receive-only")

print("Rekordbox capture regression guards: OK")
