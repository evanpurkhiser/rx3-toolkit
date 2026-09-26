<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 1.19 remote-control path

The firmware's central physical-input entry point is
`uif::IKeyManager::sendKey` at `0x0037ad64`. Its recovered ABI is:

```c
void sendKey(void *manager, int key_code, int operation, int channel,
             long value, float float_value, long auxiliary);
```

The firmware build is `/root/pdj/rbp` with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. The guarded prologue is
`f0 4f 2d e9 0c d0 4d e2`.

## Thread handoff

`sendKey` checks the message-manager object at `manager + 4`. An off-thread
caller causes it to allocate a 28-byte `KeyMsgHandler::DataSet` containing the
complete input tuple and post that object to the UI message queue.
`IKeyManager::KeyMsgHandler::onMessage` at `0x0037ca70` reads the six arguments
and calls virtual slot 2 on the key manager. The `ui::KeyManager` vtable at
`0x004d7a20` resolves that slot back to `sendKey` at `0x0037ad64`.

The network worker enters the guarded hook, which then calls the original
trampoline exactly once. Calling `IKeyManager::onKey`, a player handler, or a constructed
`KeyInput` would bypass firmware ownership, press-state tracking, long-press
timers, routing, and thread serialization.

Physical inputs originate from several contexts. `PanelComReciever` consumes
the main and sub-panel microcontroller frames, both `JogCpuCom` objects retain
the key manager, and touch and PC-control receivers use the same interface.
The module publishes unmatched physical input on the UI-thread pass. A remote
command is published immediately and marked in the pending ring; its later
UI-thread replay consumes that mark without publishing a duplicate.

## Operation and value map

The operation names are present in the firmware's own diagnostic formatter:

| Value | Operation |
| ---: | --- |
| 0 | Pressed |
| 1 | Long pressed |
| 2 | Released |
| 3 | Released after long press |
| 4 | Relative moved |
| 5 | Absolute moved |
| 6 | Value changed |

Channels 1 and 2 select a deck or mixer channel. Channel 0 is global. The
firmware also uses signed wildcard channels internally; the wire protocol does
not accept them.

Mixer knobs and faders use absolute operation 5, integer values from 0 through
1023, and a float near `value / 1023`. Buttons use press and release. Encoders
and jog pulses use relative operation 4. The tempo slider has a distinct raw
scale and should be replayed from an observed tuple until that scale is fully
labeled.

The browser rotary selector uses the signed step in both value fields. A single
clockwise detent is `(value=1, floatValue=1.0)` and a counterclockwise detent is
`(value=-1, floatValue=-1.0)`. Sending only the integer step is accepted by the
wire protocol but does not move the browser selection.

## USB media recovery after an application restart

The kernel keeps exported USB filesystems mounted when `rbp` restarts, while
the replacement application loses the one-shot mount notification that built
its media model. The remote-control preload restores that state from a detached
worker. It waits for `UsbMountManager` to open `/proc/udev_usb1`, finds the one
direct child of `/media/usb1` containing
`PIONEER/rekordbox/export.pdb`, and writes one `mount <path>` record without a
trailing newline.

The firmware 1.19 procfs implementation serializes writers with an unsafe
kernel semaphore. Concurrent writes can block and leave later shell processes
inside `udev_usb_write`. Recovery therefore sends exactly one notification and
never announces the toolkit partition. Do not replay every line from
`/proc/mounts`, trigger the block-device uevent while it is already mounted, or
read `/proc/udev_usb1`; reading consumes the next record intended for the
application.

Live validation used a two-partition SanDisk drive. The worker selected
`/media/usb1/sda2`, logged `Rekordbox USB mount replayed`, and the restarted
application opened both `export.pdb` and `exportExt.pdb`. Source selection then
reported 10 tracks, and remote Browse, rotary selection, Load 1, Play, and Cue
all completed successfully.

The complete supported host catalog is
[`tools/rx3_remote/controls.json`](../tools/rx3_remote/controls.json). It was
derived from `ui::KeyInput::keyCodeAsText()` at `0x0037cde4`. That firmware
function names 172 codes in total, including touch-screen actions, setup
pages, test controls, and calibration controls that are outside the physical
remote-control surface.

## Wire and safety model

The `RX3R` version 1 protocol listens on TCP 7357. All multibyte fields use
network byte order. The generated C layout and Python implementation live in
[`tools/rx3_remote`](../tools/rx3_remote/README.md).

The callback sends fixed-size records through a nonblocking local datagram.
If its bounded queue is full, it drops the event and marks the next delivered
event. The worker owns the TCP connection and admits one client. Each accepted
command is placed in a bounded pending-echo ring before the worker enters the
hook. The hook publishes the remote event, marks the pending tuple, and invokes
the original trampoline exactly once. When firmware replays the tuple on the
UI thread, the hook consumes the mark and suppresses the duplicate. The command
acknowledgement means the firmware handoff accepted the tuple.

Commands require a known control, operation 0 through 6, and the correct
global or per-deck channel. NaN and infinity float payloads are rejected.
Power, USB-stop, touch calibration, and test-mode inputs remain observation
only.
