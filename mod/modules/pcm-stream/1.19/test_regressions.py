# SPDX-License-Identifier: MPL-2.0
import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).parent
SOURCE = (ROOT / "rx3_pcm_stream.c").read_text()
MODULE = (ROOT / "module.sh").read_text()
MANIFEST = json.loads((ROOT / "manifest.json").read_text())


def function(name: str, following: str) -> str:
    match = re.search(
        rf"static [^\n]*\b{name}\(.*?\n\}}\n\nstatic [^\n]*\b{following}\(",
        SOURCE,
        re.S,
    )
    if match is None:
        raise AssertionError(f"could not locate {name}")
    return match.group(0)


class PcmStreamRegressionTests(unittest.TestCase):
    def test_module_is_standalone_and_exports_network_configuration(self):
        self.assertEqual(MANIFEST["requires"], [])
        self.assertNotIn("prototype", MANIFEST["name"].lower())
        self.assertIn("module_begin pcm-stream pcm_stream", MODULE)
        self.assertIn('RX3_PCM_PORT:=7355', MODULE)
        self.assertIn('RX3_PCM_BIND:=0.0.0.0', MODULE)
        self.assertIn("export RX3_PCM_PORT RX3_PCM_BIND", MODULE)
        self.assertIn("rbp_environment_value RX3_PCM_BIND", MODULE)
        self.assertIn("rbp_environment_value RX3_PCM_PORT", MODULE)
        self.assertIn("register_report_hook pcm_stream_report", MODULE)

    def test_hook_address_and_guard_match_firmware_119(self):
        self.assertIn("0x0005bb48", SOURCE)
        self.assertIn("0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1", SOURCE)

    def test_audio_callback_only_copies_and_publishes(self):
        callback = function("hooked_copy_buffer", "timespec_compare")
        for forbidden in (
            "send(", "write(", "open(", "socket(", "poll(",
            "clock_gettime(", "pthread_", "nanosleep(",
        ):
            self.assertNotIn(forbidden, callback)
        self.assertIn("memcpy(", callback)
        self.assertIn("store_release(&write_index", callback)
        self.assertIn("load_acquire(&read_index", callback)
        self.assertLess(
            callback.index("store_release(&write_index"),
            callback.index("original_copy_buffer("),
        )

    def test_queue_holds_about_a_second_without_per_slot_sample_arrays(self):
        frames = int(
            re.search(r"#define SAMPLE_RING_FRAMES (\d+)u", SOURCE).group(1)
        )
        blocks = int(
            re.search(r"#define BLOCK_RING_SLOTS (\d+)u", SOURCE).group(1)
        )
        self.assertEqual(frames & (frames - 1), 0)
        self.assertEqual(blocks & (blocks - 1), 0)
        self.assertGreaterEqual(frames, 44_100)
        self.assertLessEqual(frames, 88_200)
        slot = re.search(r"struct pcm_slot \{(.*?)\};", SOURCE, re.S).group(1)
        self.assertNotIn("samples[", slot)
        self.assertIn("sample_index", slot)

    def test_ring_publication_uses_defined_acquire_release_atomics(self):
        self.assertIn("__atomic_load_n(value, __ATOMIC_ACQUIRE)", SOURCE)
        self.assertIn("__atomic_store_n(target, value, __ATOMIC_RELEASE)", SOURCE)
        worker = SOURCE.split("static void *stream_worker", 1)[1].split(
            "__attribute__((constructor))", 1
        )[0]
        self.assertIn("load_acquire(&write_index)", worker)
        self.assertIn("store_release(&sample_read_index", worker)
        self.assertIn("store_release(&read_index", worker)
        self.assertNotIn("__sync_synchronize", SOURCE)

    def test_v1_wire_protocol_remains_compatible(self):
        sender = function("send_message", "send_config")
        self.assertIn('memcpy(header, "RX3A", 4)', sender)
        self.assertIn("header[4] = PROTOCOL_VERSION", sender)
        self.assertIn("uint8_t header[28]", sender)
        config = function("send_config", "pcm16")
        self.assertIn("big32(MAX_BLOCK_FRAMES)", config)
        self.assertIn("send_message(fd, MESSAGE_CONFIG", config)
        self.assertIn(
            "send_message(fd, MESSAGE_PCM", function("send_slot", "parse_port")
        )

    def test_socket_writes_share_one_absolute_nonblocking_deadline(self):
        send_message = function("send_message", "send_config")
        send_until = function("send_until", "send_message")
        wait_writable = function("wait_writable", "send_until")
        self.assertEqual(send_message.count("send_until("), 2)
        self.assertIn("milliseconds_until(deadline)", send_until)
        self.assertIn("EAGAIN", send_until)
        self.assertIn("EINTR", send_until)
        self.assertIn("poll(&descriptor", wait_writable)
        self.assertNotIn("SO_SNDTIMEO", SOURCE)
        self.assertIn("make_nonblocking(accepted)", SOURCE)

    def test_nan_and_infinity_are_converted_without_undefined_casts(self):
        converter = function("pcm16", "send_slot")
        self.assertIn("!(sample == sample)", converter)
        self.assertIn("sample >= 1.0f", converter)
        self.assertIn("sample <= -1.0f", converter)
        self.assertLess(converter.index("sample >= 1.0f"), converter.index("(int16_t)"))

    def test_configuration_and_atomic_status_are_present(self):
        self.assertIn('getenv("RX3_PCM_PORT")', SOURCE)
        self.assertIn('getenv("RX3_PCM_BIND")', SOURCE)
        self.assertIn('"/tmp/rx3-pcm-stream.status"', SOURCE)
        status = function("publish_status", "disconnect_client")
        for field in (
            "captured_frames=", "streamed_frames=", "dropped_frames=",
            "queue_capacity_frames=", "send_timeouts=",
        ):
            self.assertIn(field, status)
        self.assertIn("rename(STATUS_TMP_PATH, STATUS_PATH)", status)

    def test_listener_and_worker_are_ready_before_hook_activation(self):
        initialize = SOURCE.split("static void initialize(void)", 1)[1]
        self.assertLess(initialize.index("open_listener()"), initialize.index("install_hook("))
        self.assertLess(initialize.index("pthread_create("), initialize.index("install_hook("))
        self.assertIn("if (listener_fd < 0)", initialize)
        pthread_failure = initialize.split("if (pthread_create", 1)[1]
        self.assertIn("close(listener_fd)", pthread_failure)
        worker = SOURCE.split("static void *stream_worker", 1)[1].split(
            "__attribute__((constructor))", 1
        )[0]
        self.assertIn("load_acquire(&worker_command)", worker)
        self.assertIn("store_release(&worker_command, WORKER_ABORT)", initialize)
        self.assertIn("store_release(&worker_command, WORKER_RUN)", initialize)

    def test_preload_only_installs_in_rbp(self):
        self.assertIn('readlink("/proc/self/exe"', SOURCE)
        self.assertIn('expected[] = "/root/pdj/rbp"', SOURCE)
        initialize = SOURCE.split("static void initialize(void)", 1)[1]
        self.assertIn("if (!running_in_rbp())", initialize)


if __name__ == "__main__":
    unittest.main()
