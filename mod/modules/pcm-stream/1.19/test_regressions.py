# SPDX-License-Identifier: MPL-2.0
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parent
SOURCE = (ROOT / "rx3_pcm_stream.c").read_text()


class PcmStreamRegressionTests(unittest.TestCase):
    def test_hook_address_and_guard_match_firmware_119(self):
        self.assertIn("0x0005bb48", SOURCE)
        self.assertIn("0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1", SOURCE)

    def test_audio_callback_only_copies_and_publishes(self):
        callback = re.search(
            r"static int hooked_copy_buffer\(.*?\n\}\n\nstatic int send_all",
            SOURCE,
            re.S,
        ).group(0)
        for forbidden in ("send(", "write(", "open(", "socket(", "pthread_"):
            self.assertNotIn(forbidden, callback)
        self.assertIn("memcpy(", callback)
        self.assertIn("dropped_frames", callback)

    def test_preload_only_installs_in_rbp(self):
        self.assertIn('readlink("/proc/self/exe"', SOURCE)
        self.assertIn('expected[] = "/root/pdj/rbp"', SOURCE)
        initialize = SOURCE.split("static void initialize(void)", 1)[1]
        self.assertIn("if (!running_in_rbp())", initialize)

    def test_ring_dimensions_are_powers_of_two(self):
        slots = int(re.search(r"#define RING_SLOTS (\d+)u", SOURCE).group(1))
        self.assertEqual(slots & (slots - 1), 0)


if __name__ == "__main__":
    unittest.main()
