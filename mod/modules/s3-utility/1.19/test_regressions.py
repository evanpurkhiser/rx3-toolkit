#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the ESP32-S3 utility integration module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_s3_utility_feature.h").read_text()
BROKER = (ROOT.parent.parent / "core" / "1.19" / "rx3_utility_broker.h").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin s3-utility s3_utility" in MODULE,
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    MANIFEST["runtime_directory"] == "s3-utility",
    "the directory written into the image is the one module.sh is read from",
)
require(
    '[ -r "$CORE_OBJECT" ]' in MODULE,
    "the module must decline when the performance core is not selected",
)
require(
    'getenv("RX3_S3_UTILITY")' in FEATURE,
    "the feature must gate on its own environment flag",
)
require(
    "RX3_STOCK_UTILITY_COUNT 33u" in BROKER and
    "sizeof(struct rx3_native_utility_item) == 0x38u" in BROKER,
    "the broker must guard the measured 1.19 table shape",
)
require(
    all(
        slot in BROKER
        for slot in (
            "0x0013c9a8u",
            "0x0013cbccu",
            "0x0013cd1cu",
            "0x0013cf30u",
            "0x0013cfc4u",
            "0x0013d9ecu",
        )
    )
    and "rx3_utility_patch_literals" in BROKER,
    "one broker must own all firmware Utility table references",
)
require(
    "install_hook" not in FEATURE and "write_code" not in FEATURE,
    "a feature contributes rows without installing competing firmware hooks",
)

print("ESP32-S3 utility integration regression guards: OK")
