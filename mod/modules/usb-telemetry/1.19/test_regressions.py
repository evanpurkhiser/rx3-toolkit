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
    "PLAYER_STATUS_UPDATED ((unsigned long)0x002f1bf8)" in SOURCE
    and "PLAYER_LOAD_TRACK ((unsigned long)0x002f20e4)" in SOURCE
    and "PLAYER_UNLOAD_RESULT ((unsigned long)0x002f1e4c)" in SOURCE
    and "PLAYER_REF_CURRENT_TRACK ((unsigned long)0x002f1410)" in SOURCE
    and "MIXER_UPDATE_ON_AIR ((unsigned long)0x00057fb0)" in SOURCE,
    "standalone Player and Mixer hooks remain at verified entry points",
)
require(
    "GET_PLAY_MODE ((unsigned long)0x000fd960)" in SOURCE
    and "GET_PLAY_BPM ((unsigned long)0x000fd1fc)" in SOURCE
    and "GET_PLAY_TEMPO ((unsigned long)0x000fd2dc)" in SOURCE
    and "GET_MIXER_ON_AIR ((unsigned long)0x000fe34c)" in SOURCE,
    "the worker samples the standalone playback accessors",
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
    "MUSIC_ID_LOW_OFFSET 0x04u" in SOURCE
    and "MUSIC_TITLE_OFFSET 0x28u" in SOURCE
    and "read_native_deck" in SOURCE
    and "utf16_to_utf8" in SOURCE,
    "the DBIF track ID and UTF-16 title use their verified structure offsets",
)
require(
    "if (result && index >= 0 && music_info)" in SOURCE
    and "if (deck->loaded)" in SOURCE
    and "if (index >= 0 && !current_track)" in SOURCE,
    "load and unload events follow the player's committed track state",
)
require(
    "cache->generation = generation;" in SOURCE
    and "if (!delivered)" in SOURCE,
    "failed snapshots remain dirty and retain their protocol generation",
)

print("USB telemetry regression guards: OK")
