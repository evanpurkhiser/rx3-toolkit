# SPDX-License-Identifier: MPL-2.0
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parent
SOURCE = (ROOT / "rx3_remote_control.c").read_text()
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = (ROOT / "manifest.json").read_text()


class RemoteControlRegressionTests(unittest.TestCase):
    def test_send_key_address_and_guard_match_firmware_119(self):
        self.assertIn("0x0037ad64", SOURCE)
        self.assertIn(
            "0xf0, 0x4f, 0x2d, 0xe9, 0x0c, 0xd0, 0x4d, 0xe2", SOURCE
        )

    def test_hook_only_observes_and_calls_the_trampoline(self):
        callback = re.search(
            r"static void hooked_send_key\(.*?\n\}\n\nstatic int key_is_known",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("queue_event(&control, on_message_thread)", callback)
        self.assertIn("original_send_key(", callback)
        for forbidden in ("poll(", "write(", "accept(", "recv("):
            self.assertNotIn(forbidden, callback)

    def test_callback_queue_is_nonblocking(self):
        enqueue = re.search(
            r"static void queue_event\(.*?\n\}\n\nstatic void hooked_send_key",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("MSG_DONTWAIT", enqueue)
        self.assertIn("dropped_events", enqueue)

    def test_worker_enters_hook_before_firmware_thread_handoff(self):
        handler = re.search(
            r"static int handle_frame\(.*?\n\}\n\nstatic int consume_input",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("hooked_send_key(manager", handler)
        self.assertNotIn("original_send_key(manager", handler)
        self.assertIn("reserve_remote(&control)", handler)
        self.assertIn("send_ack(", handler)

    def test_remote_event_is_emitted_once_across_both_firmware_passes(self):
        classifier = re.search(
            r"static uint8_t event_source\(.*?\n\}\n\nstatic void queue_event",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("pending->emitted", classifier)
        self.assertIn("return 0", classifier)
        self.assertIn(
            "return on_message_thread ? RX3R_SOURCE_PHYSICAL : 0",
            classifier,
        )

    def test_dangerous_system_keys_are_not_in_command_allowlist(self):
        allowlist = re.search(
            r"static int key_is_known\(.*?\n\}\n\nstatic int key_uses_channel",
            SOURCE,
            re.S,
        ).group(0)
        self.assertNotIn("0x8001", allowlist)
        self.assertNotIn("0x8002", allowlist)
        self.assertNotIn("0x8100", allowlist)

    def test_preload_only_installs_in_rbp(self):
        self.assertIn('expected[] = "/root/pdj/rbp"', SOURCE)
        initialize = SOURCE.split("static void initialize(void)", 1)[1]
        self.assertIn("if (!running_in_rbp())", initialize)

    def test_runtime_is_separate_and_conflicts_with_other_send_key_hooks(self):
        self.assertIn("module_begin remote-control remote_control", MODULE)
        self.assertIn('"event-tracer"', MANIFEST)
        self.assertIn('"usb-telemetry"', MANIFEST)
        self.assertIn('"usb-link-root-shell"', MANIFEST)


if __name__ == "__main__":
    unittest.main()
