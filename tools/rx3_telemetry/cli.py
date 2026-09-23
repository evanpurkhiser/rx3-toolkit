#!/usr/bin/env python3
"""Read RX3T reports from HIDAPI or emit a deterministic simulated session."""

from __future__ import annotations

import argparse
import json
import sys

from .protocol import Decoder, encode_hello, encode_metadata, encode_state


VENDOR_ID = 0x2B73
PRODUCT_ID = 0x003D
VENDOR_USAGE_PAGES = {0xFFA0, 0xFFA1}


def emit(decoder: Decoder, report: bytes) -> None:
    for event in decoder.feed(report):
        print(json.dumps(event, separators=(",", ":")), flush=True)


def simulate() -> None:
    decoder = Decoder()
    reports = [encode_hello(0)]
    reports.extend(encode_metadata(1, sequence=1, field=1, generation=1,
                                   value="Example Track"))
    reports.extend(encode_metadata(1, sequence=3, field=2, generation=1,
                                   value="Example Artist"))
    reports.append(encode_state(
        1, sequence=5, loaded=True, on_air=False, play_state=3,
        generation=1, track_number=42, bpm_x100=12800, tempo_raw=0,
    ))
    reports.append(encode_state(
        1, sequence=6, loaded=True, on_air=True, play_state=1,
        generation=1, track_number=42, bpm_x100=12800, tempo_raw=150,
    ))
    for report in reports:
        emit(decoder, report)


def select_path(hid_module) -> bytes:
    devices = hid_module.enumerate(VENDOR_ID, PRODUCT_ID)
    candidates = [
        device for device in devices
        if device.get("usage_page") in VENDOR_USAGE_PAGES
    ]
    candidates = candidates or devices
    if not candidates:
        raise RuntimeError("XDJ-RX3 HID interface not found")
    return candidates[0]["path"]


def listen() -> None:
    try:
        import hid
    except ImportError as error:
        raise RuntimeError(
            "hidapi is required for live input; install the Python 'hidapi' package"
        ) from error

    decoder = Decoder()
    device = hid.device()
    device.open_path(select_path(hid))
    try:
        while True:
            report = device.read(21)
            if report:
                emit(decoder, bytes(report))
    finally:
        device.close()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode", choices=("listen", "simulate"), nargs="?", default="listen"
    )
    arguments = parser.parse_args()
    try:
        simulate() if arguments.mode == "simulate" else listen()
    except (RuntimeError, ValueError) as error:
        print(f"rx3-telemetry: {error}", file=sys.stderr)
        raise SystemExit(1) from error


if __name__ == "__main__":
    main()
