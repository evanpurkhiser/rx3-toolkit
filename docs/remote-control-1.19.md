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

## USB MIDI transport

The rear USB-B composite device already contains a bidirectional,
class-compliant USB MIDI 1.0 function. It uses bulk OUT endpoint 4 and bulk IN
endpoint 5 and appears inside the RX3 as ALSA card `g_pmidi`. `rbp` connects
that port to `Juce Midi Input` and `Juce Midi Output`. Its PC-control code
contains key-to-MIDI and LED-to-MIDI tables plus dedicated note, CC, jog, and
tempo-slider senders. Pioneer's published
[XDJ-RX3 MIDI message list](https://www.pioneerdj.com/-/media/pioneerdj/software-info/system/xdj-rx3/xdj-rx3_midi_message_list_e1.pdf)
documents the stock assignments.

The stock port is mode-dependent and owned by `rbp`. A custom bridge sharing
that same port would contend with JUCE and could make one MIDI message trigger
both the stock mapping and the remote-control mapping. The independent design
adds a second virtual cable to the existing MIDIStreaming interface:

| USB MIDI cable | Purpose |
| ---: | --- |
| 0 | Stock XDJ-RX3 MIDI handled by `rbp` |
| 1 | `XDJ-RX3 Remote` handled by the remote-control bridge |

Both cables can share endpoints 4 and 5, so this consumes no additional USB
endpoints and preserves the audio, HID, and sibling USB Ethernet devices. The
replacement `g_pmulti` driver must extend `f_xmidi.c` to two raw-MIDI
substreams and update Pioneer's fixed descriptors in `p_descrip.c`. A complete
USB detach and re-enumeration is required when swapping that gadget module.

The second cable can use ordinary MIDI messages and remain available to Web
MIDI without SysEx permission:

| RX3 value | MIDI representation |
| --- | --- |
| Button press and release | Note on and note off |
| 10-bit absolute control | 14-bit NRPN |
| Jog or relative encoder | Relative CC |
| Global, deck 1, deck 2 | MIDI channels 1, 2, 3 |
| Remote-origin echo | MIDI channels 9, 10, 11 |

Each catalog entry receives a stable MIDI ID from 0 through 88. Buttons fit in
the note-number range. An NRPN preserves all 1024 positions of a verified
10-bit control by scaling between 0–1023 and 0–16383. The tempo slider remains
on the raw `RX3R` transport until its distinct scale is labeled. Power and USB
Stop may be reported to the host but are rejected in the command direction.

The network protocol remains the diagnostic and lossless transport. It carries
request IDs, exact float bits, auxiliary values, device timestamps, source
labels, and schema identity that ordinary MIDI messages do not contain. The
MIDI bridge and network tools share the same `sendKey` injection and event
capture boundary.
