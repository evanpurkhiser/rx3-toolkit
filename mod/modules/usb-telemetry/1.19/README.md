<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB deck telemetry prototype

This experimental module reports loaded state, play state, on-air state, track
number, BPM, tempo, title, artist, album, and key for both decks through the
XDJ-RX3's existing rear USB-B vendor HID interface.

It is isolated from the performance core. It installs a separate `LD_PRELOAD`
library, makes no file-level byte changes to `rbp`, adds no screen controls,
and accepts only the verified firmware 1.19 application with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`.

## Event flow

Two guarded trampolines run after `rbp` updates its control-display and track
metadata caches. They only set an atomic deck bit and wake a worker through a
nonblocking local datagram socket. The worker coalesces events, reads the public
`UiHid_Get*` accessors, and owns all USB writes.

The worker also blocks on the gadget driver's sysfs connection attribute. That
attribute calls `sysfs_notify` on USB-B connect and disconnect, so attaching a
Mac produces a hello and complete two-deck snapshot without a timer. Deck state
is never periodically polled.

## Wire protocol

Every device-to-host report is exactly 20 bytes:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `RX3T` |
| 4 | 1 | protocol version, currently `1` |
| 5 | 1 | message type |
| 6 | 1 | deck number, `0` for device-wide messages |
| 7 | 1 | wrapping sequence number |
| 8 | 12 | message payload |

Message type `1` is a device hello. Type `2` is a deck-state snapshot. Type `3`
is an eight-byte metadata fragment. `tools/rx3_telemetry` is the reference
decoder and includes a simulator that does not require RX3 hardware.

## Prototype boundary

The module transmits whenever a host is connected to USB-B. It does not yet
implement the host activation handshake needed for safe coexistence with
rekordbox or Serato. Use it only with the companion reader during bench testing.

The hooks and accessor entry points are statically verified but have not run on
hardware. Metadata bytes are transported unchanged and decoded as UTF-8 with
replacement by the host until the player's exact string encoding is confirmed.
