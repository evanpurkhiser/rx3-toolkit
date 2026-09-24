<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 1.19 event tracer reference

This document records the event sources, binary interfaces, and live hardware
results for the firmware 1.19 console event tracer. The target executable is
`/root/pdj/rbp` with SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. Addresses and structure layouts
in this document apply only to that build.

The module writes newline-delimited JSON to
`/dev/shm/rx3-events.jsonl`. The file, preload library, shell, and hook state
are RAM-only and disappear at power-off. Follow a live capture from the USB
Link shell with:

```sh
tail -n 0 -f /dev/shm/rx3-events.jsonl
```

`/tmp/rx3-event-tracer.ready` appears after every guard has matched, the stream
has been created, and both decks have been sampled. Constructor and guard
failures are written to `/tmp/rx3-event-tracer.log`. The runtime launcher rolls
back to an uninstrumented `rbp` if readiness is not published.

## Event sources

The tracer combines three event sources inside `rbp`:

| Source | Purpose | Delivery |
| --- | --- | --- |
| Player commit hooks | Track identity and complete deck-state snapshots | Callback marks a deck dirty; worker reads firmware accessors |
| `DjEngineIF` hooks | Play, cue, jog, tempo, loop, beat-jump, and hot-cue requests | Callback copies arguments and the engine result |
| Central `IKeyManager::sendKey` hook | Raw panel controls, including faders and knobs | Callback copies the complete key-input tuple |

Callbacks enqueue fixed-size datagrams on a nonblocking local socket. The
worker thread performs firmware reads, JSON encoding, and file writes. A full
queue increments a counter and later produces a `dropped` record. The first
live validation produced more than 8,000 records without a drop.

The preload verifies `/proc/self/exe` is `/root/pdj/rbp` before accessing fixed
addresses or opening the event stream. This matters because helper programs
launched by `rbp` inherit `LD_PRELOAD`.

## Guarded firmware entry points

| Symbol or role | Address | Use |
| --- | ---: | --- |
| `ui::Player::statusUpdated` | `0x002f1bf8` | State-change notification |
| `ui::Player::loadTrack` | `0x002f20e4` | Committed track load |
| `ui::Player::unloadTrack` | `0x002f0f84` | Committed track unload |
| Mixer on-air update | `0x002d6250` | Channel on-air state |
| `NetworkIf::changePlayStatus` | `0x0038f278` | Raw 20-byte Pro DJ Link deck status |
| `uif::IKeyManager::sendKey` | `0x0037ad64` | Canonical physical-control dispatch |
| `ui::Player::refCurrentTrackInfo(bool)` | `0x002f1410` | Persistent current-track object |

The central control hook requires the eight-byte ARM prologue
`f0 4f 2d e9 0c d0 4d e2`. Every installed hook has a corresponding byte
guard so a different firmware build fails closed instead of patching a guessed
location.

`IKeyManager::sendKey` has this recovered interface:

```c
void sendKey(void *manager, int key_code, int operation, int channel,
             long value, float float_value, long auxiliary);
```

The corresponding firmware `KeyInput` object stores the same values at these
offsets:

| Offset | Type | Field |
| ---: | --- | --- |
| `0x00` | pointer | vtable |
| `0x04` | integer | timestamp |
| `0x08` | `uint16_t` | key code |
| `0x0a` | `uint8_t` | channel |
| `0x0b` low nibble | `uint8_t` | operation |
| `0x0c` | `int32_t` | integer value |
| `0x10` | `float` | normalized or semantic value |
| `0x14` | `int32_t` | auxiliary value |

The JSON `control` record preserves the float as `floatRawBits` so the callback
does not perform conversion. Consumers can decode it as an IEEE-754
single-precision value.

```python
import struct

value = struct.unpack("<f", struct.pack("<I", event["floatRawBits"]))[0]
```

## Physical-control key codes

The names below come from the firmware's own `ui::KeyInput::keyCodeAsText()`
table at `0x0037cde4`. `channel` distinguishes the two deck or mixer-channel
instances.

| Area | Control | Key code |
| --- | --- | ---: |
| Deck | Play/Pause | `0x4101` |
| Deck | Cue | `0x4102` |
| Deck | Vinyl mode | `0x4104` |
| Deck | Tempo range | `0x4107` |
| Deck | Master tempo | `0x4108` |
| Deck | Tempo slider | `0x4109` |
| Deck | Quantize | `0x410b` |
| Deck | Loop In | `0x410c` |
| Deck | Loop Out | `0x410d` |
| Deck | Reloop/Exit | `0x410e` |
| Deck | Auto Beat Loop | `0x4114` |
| Deck | Beat Jump | `0x4116` |
| Deck | Performance Pads 1–8 | `0x4117`–`0x411e` |
| Deck | Search forward/reverse | `0x411f`, `0x4120` |
| Deck | Jog wheel | `0x4305` |
| Deck | Jog touch | `0x4306` |
| Browser | Source | `0x0201` |
| Browser | Browse | `0x0202` |
| Browser | Rotary selector | `0x420c` |
| Browser | Back | `0x420d` |
| Browser | Load | `0x4311` |
| Mixer channel | Trim | `0x5019` |
| Mixer channel | EQ/isolator high, mid, low | `0x501a`–`0x501c` |
| Mixer channel | Channel fader | `0x501e` |
| Mixer channel | Input source | `0x501f` |
| Mixer channel | Cue | `0x5020` |
| Mixer channel | Sound Color FX knob | `0x509d` |
| Mixer channel | Sound Color FX buttons | `0x50a1`–`0x50a6` |
| Mixer channel | Sound Color FX parameter | `0x50a7` |
| Mixer/global | Crossfader | `0x6017` |
| Mixer/global | Crossfader curve switch | `0x6018` |
| Global | Master level | `0x4403` |
| Global | Booth level | `0x4404` |
| Global | Headphones mix | `0x4405` |
| Global | Headphones level | `0x4406` |
| Global | AUX level | `0x440a` |
| Beat FX | Switch, channel, on/off, time, depth, previous, next, tap | `0x448b`–`0x4492` |

Analog controls observed during startup used operation `5`. Initial button and
switch state generally used operation `2`, while jog pulses used operation `4`.
These meanings remain provisional until each operation has been exercised in a
controlled capture.

## Live hardware validation

The following behavior was observed on a running RX3 after the hooks passed
their guards and the instrumented player remained stable.

| Test | Observed result |
| --- | --- |
| Load deck 1 | Content ID 8, duration 276,036 ms, original BPM 160.05 |
| Load deck 2 | Content ID 5, duration 237,427 ms |
| Play and Cue | Raw play-mode transitions and accepted engine actions |
| Pitch slider | Continuous `tempoRaw` changes with corresponding BPM changes |
| Jog wheel | Touch, scratch, pulse, and speed events |
| Loop | In at 3,020, out at 3,770, active 2/1-beat loop, then loop exit |
| Hot cue A | `hot_cue_play` with both accepted and rejected engine results |
| Sustained capture | More than 8,000 records, approximately 1.1 MiB, zero reported drops |

The central key manager also replayed a complete physical-control snapshot at
startup. These records demonstrate that mixer faders and knobs are visible to
the hook even before a one-control-at-a-time labeling pass:

| Control | Channel | Raw | Decoded float |
| --- | ---: | ---: | ---: |
| Trim | 1 | 295 | 0.28837 |
| Trim | 2 | 281 | 0.27468 |
| EQ high, mid, and low | 1 and 2 | 512 | 0.50049 |
| Sound Color FX knob | 1 and 2 | 512 | 0.50049 |
| Channel fader | 1 and 2 | 1023 | 1.00000 |
| Crossfader | Global | 464 | 0.45357 |
| Master level | Global | 739 | 0.72239 |
| Booth level | Global | 853 | 0.83382 |
| Headphones mix | Global | 512 | 0.50049 |
| Headphones level | Global | 349 | 0.34115 |

The mixer analog values use a 0–1023 raw range and approximately
`raw / 1023.0` for the float. The tempo slider uses a different raw scale. The
startup replay is useful as an initial state snapshot; consumers should expect
it before human interaction.

## Metadata path

Content ID, duration, BPM, transport state, and timing are available. Title,
artist, album, and key remain empty for normal tracks loaded from local USB.
The string getter ABI is valid, but those getters read a separate UI-HID receive
cache that remains zero during a local load:

| Getter | Address |
| --- | ---: |
| Title | `0x00125c30` |
| Artist | `0x00125c60` |
| Album | `0x00125c90` |
| Key | `0x00125cf0` |

Each getter has the form
`int(unsigned player_no, uint16_t *destination, unsigned byte_capacity)` and
enters the shared implementation at `0x0012e820`.

The local-load metadata instead flows through `DBIF_MusicInfo` and the
persistent `ui::TrackInfo` object. `ui::TrkInfoDataSet(DBIF_MusicInfo const&,
bool)` at `0x00308544` copies 256 bytes from `DBIF_MusicInfo + 0x28`, while the
`ui::TrackInfo` constructor at `0x0030947c` places its display title beginning
at `TrackInfo + 8`. The next diagnostic capture should copy both 256-byte
regions synchronously after a committed load and emit their bytes. This will
establish the encoding before the tracer adds decoded strings. Firmware object
pointers must not be retained for worker-thread use.

## Event volume and consumer policy

Position snapshots can arrive around 50 times per second per playing deck.
Tempo, pitch, jog speed, and jog pulse can emit hundreds or thousands of
records during one gesture. Discrete actions should remain immediate. A
production bridge should retain the newest continuous value and publish on a
bounded schedule, with practical starting rates of 10 Hz for position, 20 Hz
for pitch and tempo, and 30 Hz for jog motion.

The current tracer intentionally preserves the raw stream for reverse
engineering. Its operation codes, raw play modes, and Pro DJ Link status bytes
should be labeled from controlled captures before they become a stable public
protocol.

## Semantic mixer entry points

These recovered setters provide a second observation point if a control does
not pass through the central key manager. They also expose the normalized
values consumed by the audio layer.

| Function | Address |
| --- | ---: |
| Digital trim | `0x00056e14` |
| Channel fader | `0x00056ed4` |
| Crossfader | `0x00056fc4` |
| Equalizer | `0x000570d8` |
| Beat FX on/type/time/level/percent/X-pad | `0x0005725c`–`0x0005730c` |
| Sound Color FX type/color/parameter | `0x000573c4`–`0x000574b4` |
| Master level | `0x000574f8` |
| Booth level | `0x0005753c` |
| Headphones mix | `0x000576ac` |
| Headphones level | `0x000576d0` |

The physical channel-fader handler is at `0x002d35a0`, the physical
crossfader handler at `0x002d6040`, and the player physical-key handler at
`0x00306b78`.
