#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Static guards for the ESP32-S3 utility integration module, all of them measured facts."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())
FEATURE = (ROOT / "rx3_s3_utility_feature.h").read_text()
CLIENT = (ROOT / "rx3_s3_config_client.h").read_text()
CONFIG_SOURCE = (
    ROOT.parents[3]
    / "firmware/esp32-s3-link/tools/ncm-config/rx3_ncm_config.c"
).read_text()
BROKER = (ROOT.parent.parent / "core" / "1.19" / "rx3_utility_broker.h").read_text()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


require(
    "module_begin s3-utility s3_utility" in MODULE,
    "the orchestrator loads this module under the id and namespace it declares",
)
require(
    any(file["target"] == "rx3-s3-config" and file["executable"]
        for file in MANIFEST["files"])
    and (ROOT / "rx3-s3-config").read_bytes()[:4] == b"\x7fELF"
    and int.from_bytes((ROOT / "rx3-s3-config").read_bytes()[18:20], "little") == 40,
    "the payload must carry the executable configuration command",
)
require(
    '"       %s INTERFACE set SSID\\n"' in CONFIG_SOURCE
    and "read_password(password)" in CONFIG_SOURCE
    and "tcsetattr" in CONFIG_SOURCE
    and "wipe(password" in CONFIG_SOURCE,
    "the setter must read a non-echoed password outside the process arguments",
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
            "0x0013d9e4u",
            "0x0013d9ecu",
        )
    )
    and "rx3_utility_patch_literals" in BROKER,
    "one broker must own the six table references and direct row reference",
)
require(
    "install_hook" not in FEATURE and "write_code" not in FEATURE,
    "a feature contributes rows without installing competing firmware hooks",
)
require(
    all(label in FEATURE for label in (
        '"      WI-FI STATUS"', '"      SSID"', '"      PASSWORD"',
        '"      LINK IP"', '"      ADAPTER MAC"',
    )),
    "the Utility extension must expose every live cached field",
)
require(
    "rx3_s3_worker" in CLIENT
    and "RX3_S3_CONFIG_GET_STATUS" in CLIENT
    and "rx3_s3_read_snapshot" in FEATURE
    and "rx3_s3_exchange" not in FEATURE,
    "all NCM I/O must stay in the background client, outside UI callbacks",
)
require(
    "__sync_lock_test_and_set(&rx3_s3_cache_lock" in CLIENT
    and "next.rssi = -127" in CLIENT
    and "memset(&next, 0, sizeof(next))" in CLIENT,
    "cache publication must be synchronized and failed refreshes must clear state",
)
require(
    "declared > length - RX3_S3_CONFIG_HEADER_SIZE" in CLIENT,
    "the protocol parser must allow Ethernet minimum-frame padding",
)

print("ESP32-S3 utility integration regression guards: OK")
