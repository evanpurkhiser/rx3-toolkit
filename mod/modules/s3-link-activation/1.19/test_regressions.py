#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
MANIFEST = json.loads((HERE / "manifest.json").read_text())
MODULE = (HERE / "module.sh").read_text()


class S3LinkActivationTests(unittest.TestCase):
    def test_module_requires_interface_bridge(self) -> None:
        self.assertEqual(MANIFEST["requires"], ["s3-link-bridge"])
        self.assertFalse(MANIFEST["default"])

    def test_preload_matches_container_build(self) -> None:
        expected = ROOT / "build/link-export/librx3_link_bootstrap.so"
        packaged = HERE / "librx3_link_bootstrap.so"
        self.assertEqual(
            hashlib.sha256(expected.read_bytes()).digest(),
            hashlib.sha256(packaged.read_bytes()).digest(),
        )

    def test_preload_is_installed_and_requests_one_restart(self) -> None:
        self.assertIn('register_runtime_preload "$S3_LINK_ACTIVATION_LIBRARY"', MODULE)
        self.assertIn('register_diagnostic_file "$S3_LINK_ACTIVATION_LOG"', MODULE)
        self.assertIn("s3_link_activation_normalize_preload", MODULE)
        self.assertIn("request_rbp_restart", MODULE)
        self.assertIn("S3_LINK_ACTIVATION_VERIFIED_SHA1=", MODULE)


if __name__ == "__main__":
    unittest.main()
