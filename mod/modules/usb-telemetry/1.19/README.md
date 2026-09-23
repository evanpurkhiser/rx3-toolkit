<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB deck telemetry prototype

This experimental module reports loaded state, raw play mode, on-air state,
track ID, BPM, tempo, and title for both decks through the XDJ-RX3's existing
rear USB-B vendor HID interface. Artist, album, and key fields remain present
in protocol v1 but are empty until their standalone-player sources are mapped.

It is isolated from the performance core. It installs a separate `LD_PRELOAD`
library, makes no file-level byte changes to `rbp`, adds no screen controls,
and accepts only the verified firmware 1.19 application with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`.

## Event flow

Four guarded trampolines follow the standalone Player's status, track-load, and
track-unload paths plus the Mixer's on-air update. The load hook copies the
firmware's UTF-16 title and 32-bit content ID into a small seqlock-protected
cache after a successful load. The unload hook waits for the player's committed
track reference to clear. Each change sets an atomic deck bit and wakes a worker
through a nonblocking local datagram socket. The worker coalesces events, reads
the native play-mode, BPM, tempo, and mixer-on-air accessors, and owns all USB
writes. The HID gadget queues one report, so the worker uses its blocking
backpressure to keep complete snapshots intact without blocking an `rbp`
callback.

The firmware stores track strings as UTF-16LE. The worker converts them to
UTF-8 before comparison and fragmentation on the wire.

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

The original transport ran on hardware and exposed the single-report queue's
backpressure behavior. The standalone Player/Mixer hooks and accessor entry
points are statically verified against firmware 1.19 and await hardware
validation. The firmware play-mode enum is transmitted unchanged until a
capture correlates its values with playing, paused, and cued states.
