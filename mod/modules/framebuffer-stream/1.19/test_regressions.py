# SPDX-License-Identifier: MPL-2.0
import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).parent


class FramebufferStreamGuards(unittest.TestCase):
    def test_manifest_is_transport_independent(self):
        manifest = json.loads((ROOT / "manifest.json").read_text())
        self.assertEqual(manifest["requires"], [])
        self.assertGreater(manifest["order"], 91)

    def test_protocol_and_backpressure_guards_are_present(self):
        source = (ROOT / "rx3_framebuffer_stream.c").read_text()
        self.assertIn("#define TILE_SIZE 32u", source)
        self.assertIn("#define DEFAULT_FPS 30u", source)
        self.assertIn("MSG_DONTWAIT | MSG_NOSIGNAL", source)
        self.assertIn("#define DEFAULT_PORT 7351u", source)
        self.assertIn("#define SEND_DEADLINE_MS 1000u", source)
        self.assertIn("FLAG_KEYFRAME", source)
        self.assertIn("FBIOGET_VSCREENINFO", source)
        self.assertIn("FBIOGET_FSCREENINFO", source)
        self.assertIn("int bcmp(", source)
        self.assertIn("RECT_CODEC_XOR_RLE_ZLIB", source)
        self.assertIn("RECT_CODEC_XOR_RLE_LZ4", source)
        self.assertIn("compress2", source)
        self.assertIn("build_frame_delta", source)
        self.assertIn("lz4_compress_block", source)
        self.assertIn("send_lz4_frame", source)


if __name__ == "__main__":
    unittest.main()
