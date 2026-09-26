# SPDX-License-Identifier: MPL-2.0
"""Contract tests for the JSON-configured crossfader curve module."""

from __future__ import annotations

import json
import re
import subprocess
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
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

int main(int argc, char **argv)
{{
    struct curve_config config = {{0}};
    uint32_t observations[14];
    uint16_t table[TABLE_ENTRIES];
    if (argc > 1) {{
        char contents[2048];
        ssize_t count = read(0, contents, sizeof(contents));
        (void)argv;
        return count > 0 && count < (ssize_t)sizeof(contents) &&
               parse_config_text(contents, (size_t)count, &config) ? 0 : 1;
    }}

    observations[0] = PARSE("mid 2\\n0 0\\n1 1\\n", &config);
    observations[1] = (uint32_t)config.address;
    observations[2] = config.stock_hash;
    observations[3] = PARSE("sharp 2\\n0 0\\n1 1\\n", &config);
    observations[4] = (uint32_t)config.address;
    observations[5] = config.stock_hash;
    observations[6] = PARSE("mid-half 2\\n0 0\\n1 1\\n", &config);
    observations[7] = (uint32_t)config.address;
    observations[8] = config.stock_hash;
    observations[9] = PARSE("mid 1\\n0 0\\n", &config);
    observations[10] = PARSE("mid 2\\n0.1 0\\n1 1\\n", &config);
    observations[11] = PARSE("mid 3\\n0 0\\n0.7 0.2\\n0.6 1\\n", &config);
    observations[12] = PARSE("mid 3\\n0 0\\n0.5 0.8\\n1 0.7\\n", &config);
    observations[13] = PARSE("sharp 4\\n0 0\\n0.25 0.1\\n0.75 0.9\\n1 1\\n", &config);

    generate_table(table, &config);
    (void)write(1, observations, sizeof(observations));
    (void)write(1, table, sizeof(table));
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
        table = struct.unpack("<1024H", result.stdout[observation_bytes:])
        self.assertEqual(len(result.stdout), observation_bytes + 1024 * 2)
        return observations, table

    def _canonical_is_accepted_by_preload(self, canonical):
        return subprocess.run(
            [str(self.harness_binary), "parse"],
            input=canonical,
            text=True,
            capture_output=True,
        ).returncode == 0

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

    def test_enabling_curve_preserves_and_deduplicates_other_preloads(self):
        harness = """
say() { :; }
module_begin() { :; }
register_runtime_preload() { :; }
register_prepare_hook() { :; }
register_after_launch_hook() { :; }
. "$MODULE_SCRIPT"
RBP_PRELOAD='/root/pdj/librx3_core.so:/root/pdj/librx3_crossfader_curve.so:/root/pdj/librx3_usb_telemetry.so'
crossfader_curve_with_preload
printf '%s' "$RBP_PRELOAD"
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
            "/root/pdj/librx3_crossfader_curve.so:"
            "/root/pdj/librx3_core.so:"
            "/root/pdj/librx3_usb_telemetry.so",
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
            (
                '{"version":1,"target":"mid","points":[[0,0],[1,1]]}',
                "mid 2\n0.000000000 0.000000000\n1.000000000 1.000000000",
            ),
            (
                '{\n  "points": [[0, 0], [0.25, 0.1], [1, 1]],\n'
                '  "target": "mid-half",\n  "version": 1\n}\n',
                "mid-half 3\n0.000000000 0.000000000\n"
                "0.250000000 0.100000000\n1.000000000 1.000000000",
            ),
            (
                '{ "target": "sharp", "version": 1, '
                '"points": [[0.0, 0.00], [0.5, 0.5], [1.0, 1.000]] }',
                "sharp 3\n0.000000000 0.000000000\n"
                "0.500000000 0.500000000\n1.000000000 1.000000000",
            ),
        )
        for payload, expected in cases:
            with self.subTest(payload=payload):
                result = self._validate(payload)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), expected)
                self.assertTrue(
                    self._canonical_is_accepted_by_preload(result.stdout),
                    "validator output must be accepted by the device parser",
                )

    def test_validator_preserves_a_valid_tiny_decimal_control_point(self):
        payload = (
            '{"version":1,"target":"mid","points":'
            '[[0,0],[0.000000001,0.1],[1,1]]}'
        )

        result = self._validate(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(
            self._canonical_is_accepted_by_preload(result.stdout),
            "normalization must preserve the smallest supported decimal",
        )

    def test_validator_accepts_thirty_two_control_points(self):
        points = [[0, 0], *[[index / 100, index / 100] for index in range(1, 31)], [1, 1]]
        payload = json.dumps({"points": points, "target": "sharp", "version": 1})

        result = self._validate(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        lines = result.stdout.splitlines()
        self.assertEqual(lines[0], "sharp 32")
        self.assertEqual(len(lines[1:]), 32)
        self.assertEqual(lines[1], "0.000000000 0.000000000")
        self.assertEqual(lines[-1], "1.000000000 1.000000000")
        self.assertTrue(self._canonical_is_accepted_by_preload(result.stdout))

    def test_validator_rejects_non_schema_and_malformed_json(self):
        cases = (
            "",
            "null",
            "[]",
            "{",
            '{"version":1,"target":"mid"}',
            '{"version":2,"target":"mid","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"wide","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[1,1]],"extra":true}',
            '{"version":1,"version":1,"target":"mid","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"mid","target":"sharp","points":[[0,0],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[1,1]],"points":[[0,0],[1,1]]}',
            '{"version":1,"target":"mid","points":"[[0,0],[1,1]]"}',
            '{"version":1,"target":"mid","points":[[0,0]]}',
            json.dumps({
                "version": 1,
                "target": "mid",
                "points": [[index / 32, index / 32] for index in range(33)],
            }),
            '{"version":1,"target":"mid","points":[[0.01,0],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0.01],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.99,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[1,0.99]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,0.2],[0.5,0.3],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.7,0.2],[0.6,0.3],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.4,0.7],[0.6,0.6],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[-0.1,0.2],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,1.1],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,NaN],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.5,Infinity],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],[0.0000000001,0.1],[1,1]]}',
            '{"version":1,"target":"mid","points":[[[0,0]],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0,0],[1,1]]}',
            '{"version":1,"target":"mid","points":[[0,0],["0.5",0.5],[1,1]]}',
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

    def test_generated_table_reverses_and_interpolates_public_control_points(self):
        _observations, table = self._run_c_harness()
        points = ((0.0, 0.0), (0.25, 0.1), (0.75, 0.9), (1.0, 1.0))

        def expected_sample(index):
            position = 1.0 - index / 1023.0
            for left, right in zip(points, points[1:]):
                if left[0] <= position <= right[0]:
                    amount = (position - left[0]) / (right[0] - left[0])
                    gain = left[1] + amount * (right[1] - left[1])
                    return int(gain * 32767 + 0.5)
            self.fail(f"position {position} is outside the control points")

        self.assertEqual(table[0], 32767)
        self.assertEqual(table[-1], 0)
        self.assertTrue(
            all(left >= right for left, right in zip(table, table[1:])),
            "firmware gain must decrease from index 0 to index 1023",
        )
        for index, sample in enumerate(table):
            self.assertLessEqual(abs(sample - expected_sample(index)), 1)

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
            set(example), {"version", "target", "points"},
            "the public schema must stay small and explicit",
        )
        self.assertEqual(example["version"], 1)
        self.assertIn(example["target"], {"mid", "mid-half", "sharp"})
        lowered = readme.lower()
        self.assertIn("exactly these three fields", lowered)
        self.assertRegex(lowered, r"2\s+through\s+32")
        self.assertIn("[0, 0]", readme)
        self.assertIn("[1, 1]", readme)

    def test_stock_curve_visualizations_are_accessible_svg(self):
        namespace = {"svg": "http://www.w3.org/2000/svg"}
        for name in ("stock-crossfader-curves.svg", "stock-crossfader-overlap.svg"):
            with self.subTest(name=name):
                root = ET.parse(MODULE / name).getroot()
                self.assertEqual(root.tag, "{http://www.w3.org/2000/svg}svg")
                self.assertEqual(root.get("role"), "img")
                self.assertIsNotNone(root.find("svg:title", namespace))
                self.assertIsNotNone(root.find("svg:desc", namespace))

    def test_example_configs_pass_the_device_validator(self):
        examples = MODULE / "examples"
        names = {path.name for path in examples.glob("*.json")}

        self.assertEqual(names, {"equal-power.json", "linear-center-dip.json"})
        for name in sorted(names):
            with self.subTest(name=name):
                payload = (examples / name).read_text(encoding="utf-8")
                result = self._validate(payload)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue(self._canonical_is_accepted_by_preload(result.stdout))


if __name__ == "__main__":
    unittest.main()
