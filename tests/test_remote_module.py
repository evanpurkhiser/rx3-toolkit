# SPDX-License-Identifier: MPL-2.0
import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_DIRECTORY = ROOT / "mod/modules/remote-control"
SOURCE = (MODULE_DIRECTORY / "rx3_remote_module.c").read_text()
MODULE = (MODULE_DIRECTORY / "module.sh").read_text()
MANIFEST = json.loads((MODULE_DIRECTORY / "manifest.json").read_text())


class RemoteControlModuleTests(unittest.TestCase):
    def test_current_module_contract_uses_the_shared_core(self) -> None:
        self.assertEqual(MANIFEST["firmwares"], ["1.19"])
        self.assertEqual(MANIFEST["category"], "diagnostics")
        self.assertTrue(MANIFEST["advanced"])
        self.assertEqual(MANIFEST["requires"], ["core"])
        self.assertNotIn("arm_hook", MANIFEST)
        self.assertEqual(
            MANIFEST["build_files"],
            ["rx3_remote_module.c", "rx3_remote_protocol.h"],
        )
        self.assertIn("module_export RX3_REMOTE_CONTROL 1", MODULE)
        self.assertNotIn("register_runtime_preload", MODULE)
        self.assertNotIn("librx3_remote_control", MODULE)

    def test_send_key_address_and_guard_match_firmware_119(self) -> None:
        shared = (ROOT / "mod/modules/core/services/rx3_input.c").read_text()
        self.assertIn("0x0037ad64", shared)
        self.assertIn(
            "0xf0,0x4f,0x2d,0xe9,0x0c,0xd0,0x4d,0xe2", shared
        )

    def test_observer_queues_events_without_forwarding_them(self) -> None:
        callback = re.search(
            r"static void observe_send_key\(.*?\n\}\n\nstatic int key_is_known",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("queue_event(&control, on_message_thread)", callback)
        self.assertNotIn("original_send_key(", callback)
        for forbidden in ("poll(", "write(", "accept(", "recv("):
            self.assertNotIn(forbidden, callback)

    def test_callback_queue_is_nonblocking(self) -> None:
        enqueue = re.search(
            r"static void queue_event\(.*?\n\}\n\nstatic void observe_send_key",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("MSG_DONTWAIT", enqueue)
        self.assertIn("dropped_events", enqueue)

    def test_worker_enters_hook_before_firmware_thread_handoff(self) -> None:
        handler = re.search(
            r"static int handle_frame\(.*?\n\}\n\nstatic int consume_input",
            SOURCE,
            re.S,
        ).group(0)
        self.assertIn("framework->input->dispatch_key(manager", handler)
        self.assertNotIn("original_send_key(manager", handler)
        self.assertIn("reserve_remote(&control)", handler)
        self.assertIn("send_ack(", handler)

    def test_remote_event_is_emitted_once_across_firmware_passes(self) -> None:
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

    def test_dangerous_system_keys_are_not_in_command_allowlist(self) -> None:
        allowlist = re.search(
            r"static int key_is_known\(.*?\n\}\n\nstatic int key_uses_channel",
            SOURCE,
            re.S,
        ).group(0)
        self.assertNotIn("0x8001", allowlist)
        self.assertNotIn("0x8002", allowlist)
        self.assertNotIn("0x8100", allowlist)

    def test_observer_uses_shared_input_service(self) -> None:
        self.assertIn('#include "../core/api/rx3_module_api.h"', SOURCE)
        self.assertIn("framework->input->observe_keys(&input_owner", SOURCE)
        self.assertIn("framework->input->unregister_owner(&input_owner)", SOURCE)
        self.assertNotIn("rx3_inline_hook.h", SOURCE)
        self.assertNotIn("rx3_process.h", SOURCE)
        self.assertNotIn("__attribute__((constructor))", SOURCE)

    def test_stop_closes_workers_and_drains_hook_callbacks(self) -> None:
        teardown = SOURCE.split("static void stop(void)", 1)[1]
        self.assertIn("__atomic_store_n(&running, 0u", teardown)
        self.assertIn("pthread_join(remote_thread", teardown)
        self.assertIn("pthread_join(mount_thread", teardown)
        self.assertIn("while (__atomic_load_n(&callbacks", teardown)
        self.assertIn("close_event_queue()", teardown)

    def test_usb_mount_replay_selects_only_rekordbox_partition(self) -> None:
        replay = re.search(
            r"static void \*usb_mount_replay_worker\(.*?\n\}", SOURCE, re.S
        ).group(0)
        self.assertIn("usb1_manager_is_listening()", replay)
        self.assertIn("find_rekordbox_mount(mount_path)", replay)
        self.assertIn('memcpy(notification, "mount "', replay)
        self.assertEqual(replay.count("write(fd, notification"), 1)
        self.assertIn(
            'notification_length = sizeof("mount ") - 1u + mount_length',
            replay,
        )

        selector = re.search(
            r"static int find_rekordbox_mount\(.*?\n\}", SOURCE, re.S
        ).group(0)
        self.assertIn("is_usb1_mount", selector)
        self.assertIn("has_rekordbox_export", selector)

    def test_usb_mount_replay_has_an_independent_joined_worker(self) -> None:
        startup = SOURCE.split("static int start(", 1)[1]
        self.assertIn("pthread_create(&remote_thread, 0, remote_worker", startup)
        self.assertIn(
            "pthread_create(&mount_thread, 0, usb_mount_replay_worker", startup
        )
        self.assertNotIn("pthread_detach", startup)


if __name__ == "__main__":
    unittest.main()
