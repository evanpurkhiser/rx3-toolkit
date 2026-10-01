# SPDX-License-Identifier: MPL-2.0
import json
import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_ROOT = ROOT / "mod/modules/pcm-stream"
SOURCE = (MODULE_ROOT / "rx3_pcm_stream_module.c").read_text()
SHELL = (MODULE_ROOT / "module.sh").read_text()
MANIFEST = json.loads((MODULE_ROOT / "manifest.json").read_text())
COMPOSITION = (ROOT / "mod/modules/core/runtime/rx3_composition.c").read_text()
CORE_MANIFEST = json.loads(
    (ROOT / "mod/modules/core/manifest.json").read_text()
)


def function(name: str, following: str) -> str:
    match = re.search(
        rf"static [^\n]*\b{name}\(.*?\n\}}\n\nstatic [^\n]*\b{following}\(",
        SOURCE,
        re.S,
    )
    if match is None:
        raise AssertionError(f"could not locate {name}")
    return match.group(0)


class PcmStreamTests(unittest.TestCase):
    def test_flat_manifest_composes_source_into_the_core(self):
        self.assertEqual(MANIFEST["firmwares"], ["1.19"])
        self.assertEqual(MANIFEST["requires"], ["core"])
        self.assertNotIn("arm_hook", MANIFEST)
        self.assertIn("rx3_pcm_stream_module.c", MANIFEST["build_files"])
        self.assertIn(
            "pcm-stream/rx3_pcm_stream_module.c",
            CORE_MANIFEST["arm_hook"]["sources"],
        )
        self.assertIn("rx3_pcm_stream_module", COMPOSITION)

    def test_shell_module_exports_core_configuration(self):
        self.assertIn("module_begin pcm-stream pcm_stream", SHELL)
        self.assertIn('RX3_PCM_PORT:=7355', SHELL)
        self.assertIn('RX3_PCM_BIND:=0.0.0.0', SHELL)
        for setting in ("RX3_PCM_STREAM", "RX3_PCM_PORT", "RX3_PCM_BIND"):
            self.assertIn(f"module_export {setting}", SHELL)
        self.assertNotIn("register_runtime_preload", SHELL)
        self.assertNotIn("librx3_pcm_stream", SHELL)

    def test_uses_shared_v16_hook_lifecycle(self):
        start = SOURCE.split("static int pcm_stream_start", 1)[1].split(
            "const struct rx3_module", 1
        )[0]
        stop = function("pcm_stream_stop", "pcm_stream_start")
        self.assertIn("RX3_INSTALL_HOOK(framework->install_hook", start)
        self.assertNotIn("__attribute__((constructor))", SOURCE)
        self.assertNotIn("rx3_inline_hook.h", SOURCE)
        self.assertLess(stop.index("detach_hook"), stop.index("capture_active"))
        self.assertLess(stop.index("capture_active"), stop.index("pthread_join"))
        self.assertLess(stop.index("pthread_join"), stop.index("release_hook"))

    def test_hook_address_and_guard_match_firmware_119(self):
        self.assertIn("0x0005bb48", SOURCE)
        self.assertIn(
            "0xf0, 0x40, 0x2d, 0xe9, 0x00, 0x40, 0xa0, 0xe1", SOURCE
        )

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

    def test_queue_holds_about_a_second_without_per_slot_arrays(self):
        frames = int(re.search(r"#define SAMPLE_RING_FRAMES (\d+)u", SOURCE)[1])
        blocks = int(re.search(r"#define BLOCK_RING_SLOTS (\d+)u", SOURCE)[1])
        self.assertEqual(frames & (frames - 1), 0)
        self.assertEqual(blocks & (blocks - 1), 0)
        self.assertGreaterEqual(frames, 44_100)
        self.assertLessEqual(frames, 88_200)
        slot = re.search(r"struct pcm_slot \{(.*?)\};", SOURCE, re.S)[1]
        self.assertNotIn("samples[", slot)
        self.assertIn("sample_index", slot)

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

    def test_socket_writes_have_one_nonblocking_deadline(self):
        send_message = function("send_message", "send_config")
        send_until = function("send_until", "send_message")
        wait_writable = function("wait_writable", "send_until")
        self.assertEqual(send_message.count("send_until("), 2)
        self.assertIn("milliseconds_until(deadline)", send_until)
        self.assertIn("EAGAIN", send_until)
        self.assertIn("poll(&descriptor", wait_writable)
        self.assertIn("make_nonblocking(accepted)", SOURCE)

    def test_nan_and_infinity_convert_without_undefined_casts(self):
        converter = function("pcm16", "send_slot")
        self.assertIn("!(sample == sample)", converter)
        self.assertIn("sample >= 1.0f", converter)
        self.assertIn("sample <= -1.0f", converter)
        self.assertLess(converter.index("sample >= 1.0f"), converter.index("(int16_t)"))

    def test_status_is_atomic_and_records_shutdown(self):
        status = function("publish_status", "disconnect_client")
        for field in (
            "captured_frames=", "streamed_frames=", "dropped_frames=",
            "queue_capacity_frames=", "send_timeouts=",
        ):
            self.assertIn(field, status)
        self.assertIn("rename(STATUS_TMP_PATH, STATUS_PATH)", status)
        self.assertIn('publish_status("stopped"', SOURCE)


if __name__ == "__main__":
    unittest.main()
