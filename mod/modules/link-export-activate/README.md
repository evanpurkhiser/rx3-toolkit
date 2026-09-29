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
