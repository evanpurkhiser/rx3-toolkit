# SPDX-License-Identifier: MPL-2.0
"""Inline patching stays behind the shared hook infrastructure."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"

PRIVATE_PATCHERS = {
    r"static\s+void\s*\*\s*install_hook\s*\(": "private hook installer",
    r"static\s+void\s*\*\s*prepare_hook\s*\(": "private trampoline builder",
    r"static\s+int\s+write_code\s*\(": "private executable-code writer",
    r"static\s+void\s+clear_instruction_cache\s*\(": "private cache flush",
    r"\b0xe51ff004u?\b": "private ARM absolute-branch instruction",
    r"/proc/self/exe": "private process-identity check",
}


class SharedHookInfrastructureTests(unittest.TestCase):
    def test_modules_do_not_reimplement_shared_patching(self) -> None:
        violations = []

        for path in sorted(MODULES.rglob("*")):
            if path.suffix not in {".c", ".h"}:
                continue

            source = path.read_text(encoding="utf-8")
            for pattern, description in PRIVATE_PATCHERS.items():
                if re.search(pattern, source, re.IGNORECASE):
                    violations.append(f"{path.relative_to(ROOT)}: {description}")

        self.assertEqual(
            violations,
            [],
            "modules must use mod/include/rx3_inline_hook.h and "
            "mod/include/rx3_process.h:\n" + "\n".join(violations),
        )

    def test_specialized_hook_uses_shared_install_lifecycle(self) -> None:
        core = (
            MODULES / "core/1.19/rx3_core_hook.c"
        ).read_text(encoding="utf-8")
        definition = re.search(
            r"#define RX3_INSTALL_PC_LDR_HOOK.*?(?=\n\n)",
            core,
            re.DOTALL,
        )

        self.assertIsNotNone(definition)
        self.assertIn("RX3_INSTALL_PREPARED_HOOK", definition.group())
        self.assertNotIn("activate_hook", definition.group())


if __name__ == "__main__":
    unittest.main()
