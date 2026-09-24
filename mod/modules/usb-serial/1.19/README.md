<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB serial shell prototype

This module adds a CDC ACM serial interface to the RX3's rear USB-B composite
USB device. The existing audio, MIDI, and HID functions stay in the same
configuration and retain their original interface order. A Mac should expose
the additional interface as `/dev/cu.usbmodem*`.

The runtime waits until `rbp` has stopped, unloads the stock `g_pmulti`, loads
the replacement, and then relaunches `rbp`. If the replacement does not load or
`/dev/ttyGS0` does not appear, it reloads the stock module before allowing the
old application environment to restart. Every change is in RAM and a power
cycle restores the normal firmware.

After the replacement application passes its normal readiness checks, a getty
opens `/dev/ttyGS0` and directly starts an interactive root shell. This is a
passwordless diagnostic console intended only for a direct USB cable on a
bench. It is disabled unless this module is explicitly selected.

On macOS:

```sh
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodemXXXX 115200
```

The baud value is conventional; CDC ACM transports bytes over USB and does not
use a physical baud clock. Exit the shell with `exit`, then reconnect to start a
fresh session. Power-cycle the RX3 to remove the module.

## Build provenance

`g_pmulti_acm.ko` is built from Pioneer's RX3 Linux 3.0.101 GPL source. The
corresponding source is `pmulti-acm.c`; `pmulti-acm.patch` shows the changes
against the released `pmulti.c`. It appends `f_acm` after the stock functions,
allocates one gadget serial port during bind, and releases it during unbind.
`composite-acm.patch` extends Pioneer’s fixed configuration-descriptor response
with the two ACM interfaces. This is required because the vendor build bypasses
Linux's normal dynamic descriptor generator.

The i.MX USB device controller exposes separate IN and OUT pipes for endpoint
numbers 1 through 7. Pioneer occupies OUT 1/4/6 and IN 2/5/7. ACM can therefore
allocate bulk IN 1, bulk OUT 2, and notification IN 3 without moving any stock
endpoint. Its interface IDs begin at 7, after Pioneer's fixed interfaces 0–6.

`f_paudio-gcc12.patch` replaces two legacy ARM `put_user` expansions with
`copy_to_user`. GCC 12 otherwise rejects an inline-assembly register assertion
written for Pioneer's GCC 4.6 compiler. The resulting audio ioctl has the same
observable writes and error handling.

The checked-in module was compiled inside `localhost/rx3-reverse:latest` with
networking disabled. It uses the symbol CRCs recovered from the production
1.19 kernel and has this exact vermagic:

```text
3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
```

SHA-256:

```text
a0456c5fe2d28568df6b846775f549ce05cfcc4413a236a5a4167b098a4f7189
```

## Hardware status

The module is statically verified but has not run on an RX3. The first test can
still fail during USB endpoint allocation or expose a host compatibility issue.
A failed bind restores the stock composite driver; a power cycle is the final
recovery path because no persistent firmware is written.
