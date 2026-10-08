# SPDX-License-Identifier: MPL-2.0
"""Contract tests for the JSON-configured crossfader table patch."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


REPOSITORY = Path(__file__).parents[1]
MODULE = REPOSITORY / "mod/modules/crossfader-curve"
MODULE_API = REPOSITORY / "mod/lib/module-api.sh"
sys.path.insert(0, str(REPOSITORY))

from app.runtime.build import discover_patches  # noqa: E402


TARGETS = {
    "mid": (4_299_424, "50f0b5894d6317d68e346f8bf708f754cdae7aa332e4682f177581656868891e"),
    "sharp": (4_301_472, "7b227604576e904bfbbc49eda2adb7cf3ed85586af2c56505490ae335de671d3"),
    "mid-half": (4_303_520, "e64a40fc4c7464d16f3b7e121b09f3ed58522e420b63009ffc2d0c51bf4931dc"),
}


class CrossfaderModuleContractTests(unittest.TestCase):
    def _validate(self, text: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["awk", "-f", str(MODULE / "validate-config.awk")],
            input=text,
            capture_output=True,
            text=True,
        )

    def _generate(self, canonical: str) -> bytes:
        result = subprocess.run(
            ["awk", "-f", str(MODULE / "generate-table.awk")],
            input=canonical,
            capture_output=True,
            text=True,
            check=True,
        )
        return bytes(
            int(part, 8)
            for line in result.stdout.splitlines()
            for part in line.split("\\")
            if part
        )

    def _source_module(self, payload: str | None) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module_root = root / "crossfader-curve"
            usb = root / "usb"
            shutil.copytree(MODULE, module_root)
            usb.mkdir()
            if payload is not None:
                (usb / "rx3-crossfader.json").write_text(payload, encoding="utf-8")
            script = r'''
say() { :; }
PATCH_TABLE=""
PATCH_FILE_TABLE=""
PATCH_OFFSETS=""
PATCH_SPANS=""
LOADED_MODULES=""
CURRENT_MODULE=""
CURRENT_NAMESPACE=""
MODULE_LOAD_FAILED=0
REPORT_HOOKS=""
. "$MODULE_API"
. "$MODULE_SCRIPT"
printf 'failed=%s configured=%s target=%s\n' \
  "$MODULE_LOAD_FAILED" "$CROSSFADER_CURVE_CONFIGURED" "$CROSSFADER_CURVE_TARGET"
printf '%s\n' "$PATCH_FILE_TABLE"
'''
            return subprocess.run(
                ["sh", "-c", script],
                capture_output=True,
                text=True,
                env={
                    "PATH": "/usr/bin:/bin",
                    "USB": str(usb),
                    "MODULE_API": str(MODULE_API),
                    "MODULE_SCRIPT": str(module_root / "module.sh"),
                    "CROSSFADER_CURVE_MODULE_ROOT": str(module_root),
                },
            )

    def test_manifest_packages_a_preload_free_guarded_range_module(self):
        definitions = {patch.patch_id: patch for patch in discover_patches(REPOSITORY)}
        definition = definitions["crossfader-curve"]

        self.assertIsNone(definition.arm_hook)
        self.assertEqual(
            {(item.source, item.target) for item in definition.files},
            {
                ("module.sh", "module.sh"),
                ("validate-config.awk", "validate-config.awk"),
                ("generate-table.awk", "generate-table.awk"),
                ("stock/mid.table", "stock/mid.table"),
                ("stock/sharp.table", "stock/sharp.table"),
                ("stock/mid-half.table", "stock/mid-half.table"),
            },
        )

    def test_stock_tables_are_the_verified_firmware_ranges(self):
        for target, (_offset, digest) in TARGETS.items():
            with self.subTest(target=target):
                table = (MODULE / f"stock/{target}.table").read_bytes()
                self.assertEqual(len(table), 2048)
                self.assertEqual(hashlib.sha256(table).hexdigest(), digest)

    def test_each_target_registers_one_exact_guarded_range(self):
        for target, (offset, _digest) in TARGETS.items():
            payload = json.dumps({
                "version": 1,
                "target": target,
                "points": [[0, 0], [0.5, 0.5], [1, 1]],
            })
            with self.subTest(target=target):
                result = self._source_module(payload)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"failed=0 configured=1 target={target}", result.stdout)
                rows = [line for line in result.stdout.splitlines() if line[:1].isdigit()]
                self.assertEqual(len(rows), 1)
                registered = rows[0].split()
                self.assertEqual(registered[:2], [str(offset), "2048"])
                self.assertEqual(registered[-1], f"crossfader-curve:crossfader-{target}")

    def test_missing_config_registers_no_patch(self):
        result = self._source_module(None)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("failed=0 configured=0 target=", result.stdout)
        self.assertFalse(any(line[:1].isdigit() for line in result.stdout.splitlines()))

    def test_invalid_or_oversized_config_fails_module_loading(self):
        for payload in ("{}", "x" * 4097):
            with self.subTest(size=len(payload)):
                result = self._source_module(payload)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("failed=1 configured=0", result.stdout)

    def test_validator_accepts_formatting_and_field_order(self):
        cases = (
            '{"version":1,"target":"mid","points":[[0,0],[1,1]]}',
            '{\n"points":[[0,0],[0.5,0.5],[1,1]],"target":"sharp","version":1}',
        )
        for payload in cases:
            with self.subTest(payload=payload):
                result = self._validate(payload)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_validator_rejects_invalid_schema_and_curves(self):
        cases = (
            "", "null", "[]", "{}",
            '{"version":2,"target":"mid","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"wide","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,0.8],[1,0.7]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,1.1],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[1,1]],"extra":0}',
        )
        for payload in cases:
            with self.subTest(payload=payload):
                self.assertNotEqual(self._validate(payload).returncode, 0)

    def test_generator_reverses_and_interpolates_user_points(self):
        canonical = "mid 3\n0 0\n0.5 0.5\n1 1\n"
        table = self._generate(canonical)
        samples = [int.from_bytes(table[index:index + 2], "little")
                   for index in range(0, len(table), 2)]

        self.assertEqual(len(table), 2048)
        self.assertEqual(samples[0], 32767)
        self.assertLessEqual(abs(samples[511] - 16399), 1)
        self.assertEqual(samples[-1], 0)
        self.assertTrue(all(left >= right for left, right in zip(samples, samples[1:])))

    def test_example_configs_validate_and_generate_complete_tables(self):
        names = {path.name for path in (MODULE / "examples").glob("*.json")}
        self.assertEqual(names, {"equal-power.json", "linear-center-dip.json"})
        for name in names:
            with self.subTest(name=name):
                payload = (MODULE / "examples" / name).read_text(encoding="utf-8")
                canonical = self._validate(payload)
                self.assertEqual(canonical.returncode, 0, canonical.stderr)
                self.assertEqual(len(self._generate(canonical.stdout)), 2048)

    def test_schema_and_power_behavior_are_documented(self):
        readme = (MODULE / "README.md").read_text(encoding="utf-8")
        example = re.search(r"```json\s*(\{.*?\})\s*```", readme, re.DOTALL)
        self.assertIsNotNone(example)
        self.assertEqual(set(json.loads(example.group(1))), {"version", "target", "points"})
        self.assertIn("Deck A gain² + Deck B gain²", readme)
        self.assertIn("Power cycle", readme)

    def test_stock_curve_visualizations_are_accessible_svg(self):
        namespace = {"svg": "http://www.w3.org/2000/svg"}
        for name in ("stock-crossfader-curves.svg", "stock-crossfader-overlap.svg"):
            root = ET.parse(MODULE / name).getroot()
            self.assertEqual(root.get("role"), "img")
            self.assertIsNotNone(root.find("svg:title", namespace))
            self.assertIsNotNone(root.find("svg:desc", namespace))


if __name__ == "__main__":
    unittest.main()
