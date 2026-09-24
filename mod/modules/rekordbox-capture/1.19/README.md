<!-- SPDX-License-Identifier: MPL-2.0 -->
# Rekordbox connection capture

This firmware 1.19 research module records one real computer connection while
the XDJ-RX3 continues to run its stock rekordbox Link path. It starts when the
toolkit image runs, restarts `rbp` once with a passive preload, and writes these
files to `RX3_CAPTURE` on the writable USB partition:

| File | Contents | Hard limit |
| --- | --- | ---: |
| `rekordbox-usb.jsonl` | Host-to-RX3 HID, RX3-to-host HID, and host-to-RX3 JUCE MIDI messages | 64 MiB |
| `eth0.pcap` | Complete Ethernet frames observed on `eth0`, in classic PCAP format | 128 MiB |

The HID and MIDI callbacks only copy a 256-byte bounded record into a
nonblocking local datagram. A worker thread timestamps, encodes, and writes the
records. Queue pressure produces a `dropped` record. Payloads longer than 256
bytes remain identifiable by `length`, `capturedLength`, and `truncated`.

The Ethernet recorder opens a receive-only `AF_PACKET` socket bound to `eth0`.
It does not assign an address, change routes, start a listener, send a packet,
or alter the USB gadget. This preserves the interface state used by stock Link
Export. The module conflicts with Diagnostic USB Link root shell, USB serial,
and USB telemetry so a capture image cannot also install their competing USB
or network instrumentation.

Both files are replaced when a new boot starts a capture run. Reinserting the
drive during that boot recognizes the resident preload and preserves the
existing files. Each writer stops at its hard limit. Eject the drive in the RX3
UI after the test so the FAT or exFAT filesystem can flush both open files
cleanly.

## Firmware guard

The runtime accepts only the firmware 1.19 `rbp` with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. The preload then checks the exact
eight-byte prologue at each of its three entry points before changing process
memory. If any check fails, all installed hooks are removed and readiness is
withheld, which makes the runtime restore the previous `rbp` process.

The verified entry points are:

- host-to-RX3 HID: `0x002eb864`;
- RX3-to-host HID: `0x00029568`;
- host-to-RX3 JUCE MIDI: `0x002f0238`.

## Build

Build both ARM components in the offline rootless Podman environment:

```sh
tools/rx3_rekordbox_capture/build-device.sh build/rekordbox-capture
```

The script runs the module guards inside the container and emits the preload
and freestanding PCAP recorder. Copy both outputs into this module directory
before building `autoexec.bin`. The manifest packages those verified ARM
artifacts and retains both source files as build inputs. No reverse engineering
or target build command is run directly on the host.

## Capture sequence

Build an `autoexec.bin` containing only this module and place it at the root of
the test drive. Leave the drive inserted and power on the RX3 so capture starts
before connecting rekordbox. Once the UI returns, connect the rear USB-B port
to the Mac and perform the Link test. Disconnect the Mac, eject the drive from
the RX3 UI, and inspect both files on a workstation.

This boundary does not include USB setup packets, audio isochronous payloads,
or MIDI sent from the RX3 to the host. The outbound HID hook observes the call
that queues a message and does not prove completion at the USB endpoint.
