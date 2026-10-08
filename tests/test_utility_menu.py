# SPDX-License-Identifier: MPL-2.0
"""Static contracts for the guarded Utility table extension."""

from __future__ import annotations

import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "mod/modules/core"
MENU = (CORE / "services/rx3_menu.c").read_text()
HOOK = (CORE / "rx3_core_hook.c").read_text()


class UtilityMenuTests(unittest.TestCase):
    def test_stock_table_shape_and_all_pointer_literals_are_pinned(self):
        self.assertIn("#define STOCK_COUNT 33u", MENU)
        self.assertIn("sizeof(struct utility_item) == 0x38u", MENU)
        for address in (
            "0x0013c9a8u",
            "0x0013cbccu",
            "0x0013cd1cu",
            "0x0013cf30u",
            "0x0013cfc4u",
            "0x0013d9e4u",
            "0x0013d9ecu",
        ):
            self.assertIn(address, MENU)

    def test_core_owns_installation_and_teardown(self):
        self.assertIn("int menu_active = rx3_menu_install();", HOOK)
        self.assertGreaterEqual(HOOK.count("rx3_menu_remove();"), 3)
        self.assertIn('append_heading("RX3-TOOLKIT"', MENU)
        self.assertIn('append_custom("VERSION"', MENU)
        self.assertIn('append_section("RX3-TOOLKIT"', MENU)

    def test_runtime_sources_are_evaluated_by_the_renderer(self):
        self.assertIn('getenv("RX3_MENU_ITEMS")', MENU)
        self.assertIn("VALUE_LITERAL", MENU)
        self.assertIn("VALUE_FILE", MENU)
        self.assertIn("VALUE_FIELD", MENU)
        self.assertIn("static void *update_values", MENU)
        self.assertIn("cached_value(item, value);", MENU)
        render = MENU[MENU.index("static void render_item"):MENU.index("static void render_heading")]
        self.assertNotIn("read_file", render)

    def test_menu_service_is_a_packaged_compilation_unit(self):
        manifest = json.loads((CORE / "manifest.json").read_text())
        self.assertIn("services/rx3_menu.c", manifest["build_files"])
        self.assertIn(
            "core/services/rx3_menu.c", manifest["arm_hook"]["sources"]
        )


if __name__ == "__main__":
    unittest.main()
