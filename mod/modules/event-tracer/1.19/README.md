<!-- SPDX-License-Identifier: MPL-2.0 -->
# Console event tracer prototype

This firmware 1.19 module writes newline-delimited JSON events for both decks
to `/dev/shm/rx3-events.jsonl`. From the USB Link root shell, follow the stream
with:

```sh
tail -n 0 -f /dev/shm/rx3-events.jsonl
```

The tracer reports track load and unload, title, artist, album, key, raw play
mode, playback position, cue and loop state, jog touch and scratch state, BPM,
tempo, master-tempo state, slip state, and mixer on-air state. The firmware's
play-mode and tempo values are deliberately emitted without guessed semantic
labels so a hardware capture can establish their exact meaning.

The `action` records come directly from guarded `DjEngineIF` entry points for
play, pause, tempo slider, jog speed/touch/pulse, back cue, loop in/out/exit,
reloop, automatic loops, beat jump, and hot-cue play/record/gate. `link_state`
records preserve both raw 20-byte deck records passed to Pro DJ Link's
`NetworkIf::changePlayStatus`. Action arguments remain raw during the first
capture. A `dropped` record reports any callback datagrams discarded because
the nonblocking queue was full.

Four guarded trampolines follow the standalone player's committed status,
track-load, track-unload, and mixer on-air paths. These callbacks only update a
small seqlock-protected track cache, set atomic reason bits, and wake a worker
through a nonblocking local datagram socket. The worker owns every firmware
accessor, metadata copy, JSON encoding, and file write. Additional guarded
trampolines enqueue compact action records without formatting them in the
callback. The worker emits a state group only when that group changes; there
is no timer or periodic state polling.

The preload checks `/proc/self/exe` before it touches the stream or any fixed
firmware address. This keeps helper programs launched by `rbp` from inheriting
the tracer constructor through `LD_PRELOAD`.

The JSONL file is RAM-only and is truncated before it exceeds 4 MiB. A
`stream_reset` record begins each new generation. `/tmp/rx3-event-tracer.ready`
confirms that all guards matched and the worker created the stream and sampled
both decks. Constructor failures are recorded in `/tmp/rx3-event-tracer.log`
and cause the runtime orchestrator to restore the previous `rbp` process.

The module is isolated from the performance core and conflicts with
`usb-telemetry`, since both prototypes own the same four firmware entry-point
hooks. It accepts only the verified firmware 1.19 `rbp` SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. All changes live in RAM and vanish
when the RX3 is powered off.
