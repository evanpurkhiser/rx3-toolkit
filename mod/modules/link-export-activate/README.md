<!-- SPDX-License-Identifier: MPL-2.0 -->
# Link Export activation

This firmware 1.19 module announces Link Export over whichever network
interface the player already uses. It does not choose an interface, configure
an address, or translate Pro DJ Link packets.

The shared performance core starts one worker after the player is running. The
worker waits 15 seconds, validates the firmware entry points and native object
graph, and invokes `PcController::handleUsbMountMessage` with the stock mounted
event. The callback initializes the PC-control graph, mixer key data, and MIDI
reception before setting the mounted flag sampled by `NetworkMonitor`.

If the stock state machine remains in LinkStop, the worker advances it first to
Discovery and then to Connecting. It validates each singleton pointer, vtable,
state byte, and function prologue before acting. Runtime evidence is written
through the performance core log.

The same worker invokes `PcControlCert::checkCertStatus` once through the stock
singleton. Firmware accepts Rekordbox source IDs `0x11` and `0x12` while this
certification state is active; mobile source IDs `0x29` through `0x2c` bypass
that gate. The one-shot native transition keeps PC certification active without
emulating the rear USB-B MIDI heartbeat and its one-second lease.
