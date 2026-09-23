# SPDX-License-Identifier: MPL-2.0
"""Contract tests for the JSON-configured crossfader curve module."""

from __future__ import annotations

import json
import math
import re
import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).parents[1]
MODULE = REPOSITORY / "mod/modules/crossfader-curve/1.19"
sys.path.insert(0, str(REPOSITORY))

from tools.rx3_runtime.build import discover_patches  # noqa: E402


class CrossfaderModuleContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory()
        harness = Path(cls._build_directory.name) / "crossfader_harness.c"
        cls.harness_binary = Path(cls._build_directory.name) / "crossfader_harness"
        source = MODULE / "rx3_crossfader_curve.c"
        harness.write_text(
            f'''\
#include "{source}"

#define PARSE(text, config) parse_config_text((text), sizeof(text) - 1u, (config))

int main(void)
{{
    struct curve_config config = {{0}};
    uint32_t observations[14];
    uint16_t tables[3][TABLE_ENTRIES];

    observations[0] = PARSE("mid -6\\n", &config);
    observations[1] = (uint32_t)config.address;
    observations[2] = config.stock_hash;
    observations[3] = PARSE("sharp -6\\n", &config);
    observations[4] = (uint32_t)config.address;
    observations[5] = config.stock_hash;
    observations[6] = PARSE("mid-half -6\\n", &config);
    observations[7] = (uint32_t)config.address;
    observations[8] = config.stock_hash;
    observations[9] = PARSE("mid -6 extra\\n", &config);
    observations[10] = PARSE("m id -6\\n", &config);
    observations[11] = PARSE("mid -48.01\\n", &config);
    observations[12] = PARSE("mid -3.0102\\n", &config);
    observations[13] = PARSE("sharp -3.0103\\n", &config);

    generate_table(tables[0], -48.0);
    generate_table(tables[1], -6.0);
    generate_table(tables[2], -3.0103);
    (void)write(1, observations, sizeof(observations));
    (void)write(1, tables, sizeof(tables));
    return 0;
}}
''',
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                "cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fno-builtin",
                str(harness), "-o", str(cls.harness_binary),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode:
            raise AssertionError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def _run_c_harness(self):
        result = subprocess.run(
            [str(self.harness_binary)], check=True, capture_output=True
        )
        observation_bytes = 14 * 4
        observations = struct.unpack("<14I", result.stdout[:observation_bytes])
        tables = struct.unpack("<3072H", result.stdout[observation_bytes:])
        self.assertEqual(len(result.stdout), observation_bytes + 3072 * 2)
        return observations, [
            tables[index:index + 1024] for index in range(0, 3072, 1024)
        ]

    def test_manifest_is_discoverable_and_packages_the_runtime(self):
        definitions = {
            patch.patch_id: patch
            for patch in discover_patches(REPOSITORY, firmware="1.19")
        }

        self.assertIn("crossfader-curve", definitions)
        definition = definitions["crossfader-curve"]
        self.assertEqual(definition.runtime_directory, "crossfader-curve")
        self.assertEqual(definition.namespace, "crossfader_curve")
        self.assertTrue(definition.selectable)
        self.assertFalse(definition.default)
        self.assertIsNotNone(definition.arm_hook)
        self.assertEqual(definition.arm_hook.source, "rx3_crossfader_curve.c")
        self.assertEqual(definition.arm_hook.target, "librx3_crossfader_curve.so")
        self.assertEqual(
            [(item.source, item.target) for item in definition.files],
            [
                ("module.sh", "module.sh"),
                ("validate-config.awk", "validate-config.awk"),
            ],
        )

    def test_module_uses_a_fixed_usb_root_config_and_exact_firmware_gate(self):
        script = (MODULE / "module.sh").read_text(encoding="utf-8")

        self.assertIn("$USB/rx3-crossfader.json", script)
        self.assertIn("/root/pdj/rx3-crossfader.config", script)
        hashes = re.findall(r"\b[0-9a-f]{40}\b", script)
        self.assertEqual(len(set(hashes)), 1)
        self.assertIn('"$ACCEPTED" = "$CROSSFADER_CURVE_VERIFIED_SHA1"', script)
        self.assertIn("register_runtime_preload", script)

    def test_missing_config_does_not_install_or_restart_a_fresh_session(self):
        with tempfile.TemporaryDirectory() as usb:
            harness = """
say() { :; }
module_begin() { :; }
register_ready_file() { :; }
register_diagnostic_file() { :; }
register_runtime_preload() { :; }
register_prepare_hook() { :; }
register_after_launch_hook() { :; }
preload_without_runtime() { printf '%s' "$1"; }
preload_contains() { return 1; }
request_rbp_restart() { NEED_RBP_RESTART=1; }
NEED_RBP_RESTART=0
RBP_PRELOAD=
ACCEPTED=cf309238491e73cdbdc1f08a09f7a3177e079068
. "$MODULE_SCRIPT"
crossfader_curve_prepare
printf 'restart=%s preload=%s\n' "$NEED_RBP_RESTART" "$RBP_PRELOAD"
"""
            result = subprocess.run(
                ["sh", "-c", harness],
                check=False,
                capture_output=True,
                text=True,
                env={
                    "USB": usb,
                    "MODULE_SCRIPT": str(MODULE / "module.sh"),
                    "PATH": "/usr/bin:/bin",
                },
            )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "restart=0 preload=\n")

    def test_normalization_preserves_other_runtime_preloads(self):
        harness = """
say() { :; }
module_begin() { :; }
register_runtime_preload() { :; }
register_prepare_hook() { :; }
register_after_launch_hook() { :; }
. "$MODULE_SCRIPT"
crossfader_curve_without_preload \
  '/root/pdj/librx3_core.so:/root/pdj/librx3_crossfader_curve.so:/root/pdj/librx3_usb_telemetry.so'
"""
        result = subprocess.run(
            ["sh", "-c", harness],
            check=False,
            capture_output=True,
            text=True,
            env={
                "USB": "/mnt/usb",
                "MODULE_SCRIPT": str(MODULE / "module.sh"),
                "PATH": "/usr/bin:/bin",
            },
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "/root/pdj/librx3_core.so:/root/pdj/librx3_usb_telemetry.so",
        )

    def _validate(self, text):
        return subprocess.run(
            ["awk", "-f", str(MODULE / "validate-config.awk")],
            input=text,
            capture_output=True,
            text=True,
        )

    def test_validator_accepts_standard_json_formatting_and_field_order(self):
        cases = (
            ('{"version":1,"target":"mid","midpoint_db":-6.0}', "mid -6.0"),
            (' { "midpoint_db": -12, "version": 1, "target": "mid-half" } ', "mid-half -12"),
            ('{\n  "target": "sharp",\n  "midpoint_db": -3.0103,\n  "version": 1\n}\n', "sharp -3.0103"),
            ('{"target":"mid","version":1,"midpoint_db":-48}', "mid -48"),
        )
        for payload, expected in cases:
            with self.subTest(payload=payload):
                result = self._validate(payload)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)

    def test_validator_rejects_non_schema_and_malformed_json(self):
        cases = (
            "",
            "null",
            "[]",
            "{",
            '{"version":1,"target":"mid"}',
            '{"version":2,"target":"mid","midpoint_db":-6}',
            '{"version":1,"target":"wide","midpoint_db":-6}',
            '{"version":1,"target":"mid","midpoint_db":-6,"extra":true}',
            '{"version":1,"version":1,"target":"mid","midpoint_db":-6}',
            '{"version":1,"target":"mid","target":"sharp","midpoint_db":-6}',
            '{"version":1,"target":"mid","midpoint_db":-6,"midpoint_db":-7}',
            '{"version":1,"target":"mid","midpoint_db":"-6"}',
            '{"version":1,"target":"mid","midpoint_db":NaN}',
            '{"version":1,"target":"m id","midpoint_db":-6}',
            '{"version":1,"target":"mid","midpoint_db":- 6}',
            '{"version":1,"target":"mid","midpoint_db":-48.01}',
            '{"version":1,"target":"mid","midpoint_db":-3.0102}',
        )
        for payload in cases:
            with self.subTest(payload=payload):
                result = self._validate(payload)
                self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_preload_parser_maps_each_target_to_the_verified_stock_table(self):
        observations, _tables = self._run_c_harness()

        self.assertEqual(
            observations[:9],
            (
                1, 0x00421AA0, 0xE61C0A56,
                1, 0x004222A0, 0x99BBFE6F,
                1, 0x00422AA0, 0xB70FFE4A,
            ),
        )

    def test_preload_parser_rejects_malformed_and_out_of_range_values(self):
        observations, _tables = self._run_c_harness()

        self.assertEqual(observations[9:13], (0, 0, 0, 0))
        self.assertEqual(observations[13], 1)

    def test_generated_tables_have_exact_endpoints_and_monotonic_gain(self):
        _observations, tables = self._run_c_harness()

        for midpoint_db, table in zip((-48.0, -6.0, -3.0103), tables):
            with self.subTest(midpoint_db=midpoint_db):
                self.assertEqual(table[0], 32767)
                self.assertEqual(table[-1], 0)
                self.assertTrue(
                    all(left >= right for left, right in zip(table, table[1:])),
                    "gain must never rise as the fader moves toward the endpoint",
                )
                measured_db = 20.0 * math.log10(table[512] / 32767.0)
                self.assertAlmostEqual(measured_db, midpoint_db, delta=0.12)
                self.assertLessEqual(table[512], 23170, "midpoint must not exceed unity power")
                power_limit = 32767**2 + 2 * 32767
                self.assertTrue(
                    all(
                        table[index] ** 2 + table[-1 - index] ** 2 <= power_limit
                        for index in range(512)
                    ),
                    "mirrored deck gains must not create a power bump",
                )

    def test_stock_hash_guard_runs_before_memory_is_made_writable(self):
        source = (MODULE / "rx3_crossfader_curve.c").read_text(encoding="utf-8")
        replace = re.search(
            r"static int replace_table\(.*?\n\}", source, re.DOTALL
        )
        self.assertIsNotNone(replace)
        body = replace.group(0)
        self.assertLess(body.index("table_hash"), body.index("mprotect"))
        self.assertLess(body.index("table_hash"), body.index("memcpy"))

    def test_schema_is_documented_as_closed_and_versioned(self):
        readme = (MODULE / "README.md").read_text(encoding="utf-8")

        examples = re.findall(r"```json\s*(\{.*?\})\s*```", readme, re.DOTALL)
        self.assertTrue(examples, "README must contain a machine-readable example")
        example = json.loads(examples[0])
        self.assertEqual(
            set(example), {"version", "target", "midpoint_db"},
            "the public schema must stay small and explicit",
        )
        self.assertEqual(example["version"], 1)
        self.assertIn(example["target"], {"mid", "mid-half", "sharp"})
        lowered = readme.lower()
        self.assertIn("exactly these three fields", lowered)
        self.assertIn("-48", readme)
        self.assertIn("-3.0103", readme)


if __name__ == "__main__":
    unittest.main()
