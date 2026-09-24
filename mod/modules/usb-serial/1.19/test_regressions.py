# SPDX-License-Identifier: MPL-2.0
from __future__ import annotations

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[3]
MODULE = (ROOT / "module.sh").read_text(encoding="utf-8")
SOURCE = (ROOT / "pmulti-acm.c").read_text(encoding="utf-8")
COMPOSITE_PATCH = (ROOT / "composite-acm.patch").read_text(encoding="utf-8")
MANIFEST = json.loads((ROOT / "manifest.json").read_text(encoding="utf-8"))
ORCHESTRATOR = (REPOSITORY / "mod/autoexec.sh").read_text(encoding="utf-8")
IMAGE = ROOT / "g_pmulti_acm.ko"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require(MANIFEST["default"] is False, "a root shell must be opt-in")
    require("usb-telemetry" in MANIFEST["conflicts"],
            "two rear USB-B experimental transports cannot be selected")
    require("register_stopped_hook usb_serial_swap_gadget" in MODULE,
            "the gadget must be replaced only after rbp releases its nodes")
    require("usb_serial_restore_stock" in MODULE,
            "a failed experimental bind must restore the stock gadget")

    hid = SOURCE.index("hidg_bind_config")
    acm = SOURCE.index("acm_bind_config")
    require(acm > hid, "CDC ACM must append after every stock USB interface")
    require("c->next_interface_id = USBF_IFNUM_AUDIO_STRM_IN + 1;" in SOURCE,
            "ACM IDs must start after Pioneer's fixed interface numbers")
    require("gserial_setup(gadget, 1)" in SOURCE,
            "the ACM function needs a ttyGS backing port")
    require("gserial_cleanup();" in SOURCE,
            "the ttyGS allocation must be released on unbind")
    require("append_acm_descriptors" in COMPOSITE_PATCH,
            "Pioneer's fixed descriptor blob must advertise the ACM function")
    require("descriptor->bNumInterfaces = config->next_interface_id" in COMPOSITE_PATCH,
            "the advertised interface count must include ACM control and data")

    stopped = ORCHESTRATOR.index('run_hooks "$STOPPED_HOOKS"')
    killed = ORCHESTRATOR.index('say "rbp stopped after ${i}s"')
    write = ORCHESTRATOR.index("write_words patched")
    require(killed < stopped < write,
            "stopped hooks must run after rbp exits and before mutation/launch")
    stopped_failure = ORCHESTRATOR[stopped:write]
    require('launch_rbp "$RBP_RESTORE_OUTPUT"' in stopped_failure,
            "a stopped-hook failure must relaunch the previous environment")
    require('rm -rf "$TMP"; sync; exit 1' in stopped_failure,
            "a stopped-hook failure must prevent the replacement launch")

    digest = hashlib.sha256(IMAGE.read_bytes()).hexdigest()
    require(digest == "a0456c5fe2d28568df6b846775f549ce05cfcc4413a236a5a4167b098a4f7189",
            "the reviewed production-ABI module changed")
    blob = IMAGE.read_bytes()
    require(b"3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7" in blob,
            "kernel vermagic does not match RX3 firmware 1.19")


if __name__ == "__main__":
    main()
