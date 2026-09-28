<!-- SPDX-License-Identifier: MPL-2.0 -->
# Link Export activation

This standalone firmware 1.19 module forces the RX3 to announce Link Export
over whichever network interface the player already uses. It preloads a
firmware-guarded shim into `rbp`, invokes
`PcController::handleUsbMountMessage` with the stock mounted event, and resumes
the stock Link state machine when it remains stopped.

The module has no transport dependency. The active interface may be rear
USB-B, an Ethernet adapter, a Wi-Fi bridge, or another network path configured
before `rbp` starts. This module does not select that interface, configure its
address, or translate Pro DJ Link packets.

The mounted callback initializes the PC-control object graph, mixer key data,
and MIDI reception, then sets the flag sampled by `NetworkMonitor`. The shim
uses the native state machine instead of writing the mounted byte directly. It
only advances Discovery or Connecting when the application remains in its
normal LinkStop state.

The constructor waits 15 seconds before touching firmware objects, then checks
the expected instructions, singleton pointers, vtables, and mounted result.
The delay is a conservative startup guard rather than a protocol timeout.
Runtime evidence is written to `/tmp/rx3-link-export-activate.log`.

The shim also invokes `PcControlCert::checkCertStatus` once through the stock
singleton. Firmware accepts an `0x11` or `0x12` Rekordbox source only when this
certification state is active; `0x29` through `0x2c` bypass that gate as mobile
sources. The one-shot native transition keeps PC certification active without
emulating the rear USB-B MIDI heartbeat and its one-second lease.
