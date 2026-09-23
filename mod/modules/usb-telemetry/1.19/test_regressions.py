#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the USB telemetry module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
SOURCE = (ROOT / "rx3_usb_telemetry.c").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin usb-telemetry usb_telemetry" in MODULE,
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    MANIFEST["arm_hook"]["target"] == "librx3_usb_telemetry.so",
    "the module packages its isolated preload",
)
require(
    'register_runtime_preload "$USB_TELEMETRY_LIB"' in MODULE,
    "rollback removes the telemetry preload",
)
require(
    "cf309238491e73cdbdc1f08a09f7a3177e079068" in MODULE,
    "only the statically verified rbp build loads this prototype",
)
require(
    "register_patch" not in MODULE,
    "telemetry makes no file-level rbp byte patch",
)
require(
    "CONTROL_DISPLAY_UPDATED ((unsigned long)0x0012a2cc)" in SOURCE
    and "TRACK_APP_INFO_UPDATED ((unsigned long)0x0012e358)" in SOURCE,
    "event hooks remain at the statically verified entry points",
)
require(
    "poll(descriptors, 2u, -1)" in SOURCE,
    "the worker blocks on state and USB connection events",
)
require(
    "SOCK_DGRAM | SOCK_NONBLOCK" in SOURCE and "MSG_DONTWAIT" in SOURCE,
    "event callbacks can never block rbp",
)
require(
    'HID_PATH "/dev/hidg0"' in SOURCE and "#define REPORT_SIZE 20u" in SOURCE,
    "the prototype uses the existing fixed-size vendor HID transport",
)
require(
    "open(HID_PATH, O_WRONLY)" in SOURCE
    and "open(HID_PATH, O_WRONLY | O_NONBLOCK)" not in SOURCE,
    "the worker applies HID gadget backpressure instead of dropping a burst",
)
require(
    "typedef void (*get_string_fn)(unsigned int, uint16_t *, unsigned int);"
    in SOURCE
    and "append_utf8" in SOURCE,
    "firmware UTF-16 metadata is converted before protocol fragmentation",
)
require(
    "cache->generation = generation;" in SOURCE
    and "if (!delivered)" in SOURCE,
    "failed snapshots remain dirty and retain their protocol generation",
)

print("USB telemetry regression guards: OK")
