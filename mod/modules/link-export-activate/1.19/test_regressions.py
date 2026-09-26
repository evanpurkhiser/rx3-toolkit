#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

import json
from pathlib import Path
import unittest


HERE = Path(__file__).resolve().parent
MANIFEST = json.loads((HERE / "manifest.json").read_text())
MODULE = (HERE / "module.sh").read_text()


class LinkExportActivateTests(unittest.TestCase):
    def test_module_is_transport_independent(self) -> None:
        self.assertEqual(MANIFEST["id"], "link-export-activate")
        self.assertEqual(MANIFEST["runtime_directory"], "link-export-activate")
        self.assertEqual(MANIFEST["namespace"], "link_export_activate")
        self.assertEqual(MANIFEST["requires"], [])
        self.assertFalse(MANIFEST["default"])

    def test_preload_builds_from_module_source(self) -> None:
        hook = MANIFEST["arm_hook"]
        self.assertEqual(hook["source"], "rx3_link_export_activate.c")
        self.assertEqual(hook["target"], "librx3_link_export_activate.so")
        self.assertTrue((HERE / hook["source"]).is_file())

    def test_preload_is_installed_and_requests_one_restart(self) -> None:
        self.assertIn('module_begin link-export-activate link_export_activate', MODULE)
        self.assertIn('register_runtime_preload "$LINK_EXPORT_ACTIVATE_LIBRARY"', MODULE)
        self.assertIn('register_diagnostic_file "$LINK_EXPORT_ACTIVATE_LOG"', MODULE)
        self.assertIn("link_export_activate_normalize_preload", MODULE)
        self.assertIn("request_rbp_restart", MODULE)
        self.assertIn("LINK_EXPORT_ACTIVATE_VERIFIED_SHA1=", MODULE)

if __name__ == "__main__":
    unittest.main()
