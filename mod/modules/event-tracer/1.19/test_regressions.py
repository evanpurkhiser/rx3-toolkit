#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static safety guards for the firmware 1.19 console event tracer."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
SOURCE = (ROOT / "rx3_event_tracer.c").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require("module_begin event-tracer event_tracer" in MODULE, "module identity drifted")
require(MANIFEST["conflicts"] == ["usb-telemetry"], "overlapping hooks must conflict")
require(
    MANIFEST["arm_hook"]["target"] == "librx3_event_tracer.so",
    "the ARM preload must be packaged",
)
require(
    'register_runtime_preload "$EVENT_TRACER_LIB"' in MODULE,
    "rollback must remove the tracer preload",
)
require(
    "cf309238491e73cdbdc1f08a09f7a3177e079068" in MODULE,
    "only the verified firmware application may load the tracer",
)
require("register_patch" not in MODULE, "the tracer must not modify rbp on disk")

for address in (
    "0x002f1bf8", "0x002f20e4", "0x002f1e4c", "0x00057fb0",
    "0x000455b4", "0x00045744", "0x00045e6c", "0x00046d2c",
    "0x00046de4", "0x0004736c", "0x00047914", "0x000479cc",
    "0x00047b3c", "0x00047bf4", "0x000480c4", "0x00048184",
    "0x000488c8", "0x00048980", "0x00048c68", "0x00048f40",
    "0x00049ae0", "0x0038f278",
):
    require(address in SOURCE, f"verified hook {address} is missing")

for accessor in (
    "GET_TITLE_STRING", "GET_ARTIST_STRING", "GET_ALBUM_STRING",
    "GET_KEY_STRING", "GET_PLAY_MODE", "GET_PLAY_TIME",
    "GET_PLAY_JOG_TOUCHED", "GET_PLAY_JOG_SCRATCHING",
    "GET_PLAY_TEMPO_RATE", "GET_CURRENT_CUE_EXIST",
    "GET_CURRENT_CUE_IN", "GET_CURRENT_CUE_OUT", "GET_CURRENT_CUE_LOOP",
):
    require(accessor in SOURCE, f"state accessor {accessor} is missing")

require(
    "SOCK_DGRAM | SOCK_NONBLOCK" in SOURCE and "MSG_DONTWAIT" in SOURCE,
    "callbacks must use a nonblocking datagram queue",
)
require(
    "dropped_actions" in SOURCE and 'json_prefix(&json, "dropped"' in SOURCE,
    "queue pressure must be observable",
)
require(
    '#define EVENT_PATH "/dev/shm/rx3-events.jsonl"' in SOURCE
    and "EVENT_LIMIT (4u * 1024u * 1024u)" in SOURCE,
    "the bounded event stream must remain in RAM",
)
require(
    "poll(&descriptor, 1u, -1)" in SOURCE,
    "the worker must block on events rather than poll firmware state",
)
initialize = SOURCE.index("__attribute__((constructor)) static void initialize(void)")
gate = SOURCE.index("if (!running_in_rbp())", initialize)
guards = SOURCE.index("if (!accessor_guards_match())", initialize)
require(
    gate < guards
    and 'readlink("/proc/self/exe"' in SOURCE
    and 'static const char expected[] = "/root/pdj/rbp"' in SOURCE,
    "foreign child processes must return before fixed-address firmware access",
)
require(
    "if (!reset_event_stream())" in SOURCE
    and SOURCE.index("publish_ready();", SOURCE.index("static void *event_loop"))
    < SOURCE.index("for (;;)", SOURCE.index("static void *event_loop")),
    "the worker must create and seed the stream before publishing readiness",
)
require(
    SOURCE.count("write_json(&json);") >= 10,
    "the JSON worker lost expected event families",
)

print("Event tracer regression guards: OK")
