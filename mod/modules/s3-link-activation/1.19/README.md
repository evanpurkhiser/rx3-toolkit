<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 Link application activation

This firmware 1.19 module complements `s3-link-bridge`. The bridge redirects
`rbp` to the S3 NCM device's `usb0` interface before `rbp` starts. This
module preloads a firmware-guarded shim into that replacement process and
invokes `PcController::handleUsbMountMessage` with the stock mounted event.

The mounted callback initializes the PC-control object graph, mixer key data,
and MIDI reception, then sets the flag sampled by `NetworkMonitor`. The shim
uses the native state machine instead of writing the mounted byte directly. It
only advances Discovery or Connecting when the application remains in its
normal LinkStop state.

The constructor waits 15 seconds before touching firmware objects, then checks
the expected instructions, singleton pointers, vtables, and mounted result.
The delay is a conservative startup guard rather than a protocol timeout.
Runtime evidence is written to `/tmp/rx3-link-bootstrap.log`.

The captured USB-MIDI activation command maintains a separate one-second
`PcControlCert` lease. Static analysis shows that `NetworkMonitor` gates Link
state on the mounted flag rather than `PcController::isCertified`, so this
module begins with the mounted callback alone. `PcControlCert::checkCertStatus`
is the native one-shot extension if live UI evidence shows certification is
also required.
