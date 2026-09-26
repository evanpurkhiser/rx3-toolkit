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

The captured USB-MIDI activation command maintains a separate one-second
`PcControlCert` lease. Static analysis shows that `NetworkMonitor` gates Link
state on the mounted flag rather than `PcController::isCertified`, so this
module begins with the mounted callback alone. `PcControlCert::checkCertStatus`
is the native one-shot extension if live UI evidence shows certification is
also required.
