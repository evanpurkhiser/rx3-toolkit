# SPDX-License-Identifier: MPL-2.0
import hashlib
import os
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RECIPE = ROOT / "tools" / "rx3_vpu_kernel"


class VpuKernelRecipeTests(unittest.TestCase):
    def test_recipe_matches_the_shared_kernel_builder_contract(self):
        required = {
            "build.sh",
            "modules.list",
            "production.symvers",
            "production.symvers.sha256",
        }

        self.assertTrue(required.issubset(path.name for path in RECIPE.iterdir()))
        self.assertTrue(os.access(RECIPE / "build.sh", os.X_OK))

        build = (RECIPE / "build.sh").read_text(encoding="utf-8")
        self.assertIn(". /tool/kernel-build-lib.sh", build)
        self.assertIn("rx3_kernel_prepare", build)
        self.assertIn("rx3_kernel_make", build)
        self.assertIn('cp /build/mxc_vpu.ko "$output_directory/"', build)

        self.assertEqual(
            (RECIPE / "modules.list").read_text(encoding="ascii"),
            "mxc_vpu.ko\n",
        )

    def test_production_symbol_profile_matches_its_pin(self):
        profile = RECIPE / "production.symvers"
        expected = (RECIPE / "production.symvers.sha256").read_text(
            encoding="ascii"
        ).split()[0]

        self.assertEqual(hashlib.sha256(profile.read_bytes()).hexdigest(), expected)


if __name__ == "__main__":
    unittest.main()
