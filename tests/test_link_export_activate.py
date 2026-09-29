# SPDX-License-Identifier: MPL-2.0
"""Executable contracts for the composed Link Export module."""

from __future__ import annotations

import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE = ROOT / "mod/modules/link-export-activate"


class LinkExportActivateTests(unittest.TestCase):
    def test_manifest_uses_the_shared_core(self):
        manifest = json.loads((MODULE / "manifest.json").read_text())
        self.assertEqual(manifest["firmwares"], ["1.19"])
        self.assertEqual(manifest["requires"], ["core"])
        self.assertNotIn("arm_hook", manifest)
        self.assertEqual(
            manifest["build_files"], ["rx3_link_export_activate_module.c"]
        )

    def test_worker_runs_the_stock_transition_once(self):
        compiler = shutil.which("cc") or shutil.which("clang")
        if not compiler:
            self.skipTest("native C compiler required")
        source = r'''
#include "link-export-activate/rx3_link_export_activate_module.c"
static unsigned char controller[256];
static unsigned int events[16], event_count;
static void log_message(const char *line) {(void)line;}
static int guards(void) {events[event_count++]=20;return 1;}
static void *find(void) {events[event_count++]=30;return controller;}
static void mount(void *pc) {events[event_count++]=40;((unsigned char *)pc)[0x72]=1;}
static int change(int state) {events[event_count++]=(unsigned int)state;return 1;}
static int pause_now(unsigned int seconds) {events[event_count++]=seconds;return 1;}
int main(void) {
    static const struct rx3_services services={.log_line=log_message};
    framework=&services;worker_running=1;
    operations.guards_match=guards;operations.find_controller=find;
    operations.mount=mount;operations.change_state=change;operations.pause=pause_now;
    activate_link_export(0);
    const unsigned int expected[]={15,20,30,40,3,1,5,5};
    assert(event_count==sizeof(expected)/sizeof(expected[0]));
    for(unsigned int i=0;i<event_count;i++)assert(events[i]==expected[i]);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            host = directory / "host.h"
            host.write_text(
                "#define RX3_PLATFORM_H\n"
                "#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n"
                "#include <stdlib.h>\n#include <string.h>\n#include <pthread.h>\n"
                "#include <unistd.h>\nextern int usleep(unsigned int);\n"
            )
            unit = directory / "test.c"
            unit.write_text(source)
            binary = directory / "test"
            result = subprocess.run(
                [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                 "-include", str(host), "-I", str(ROOT / "mod/modules"),
                 str(unit), "-pthread", "-o", str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
