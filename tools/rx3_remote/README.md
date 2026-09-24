# RX3 remote-control host tools

This directory defines the host half of the binary `RX3R/1` protocol. The RX3
module listens on TCP port 7357 over the USB Link Ethernet interface. It sends
physical control events to subscribed clients and accepts the exact tuple used
by firmware 1.19's `uif::IKeyManager::sendKey` function.

List the currently recovered control names:

```sh
python -m tools.rx3_remote.cli controls
```

Follow physical events, reconnecting whenever the application or link restarts:

```sh
python -m tools.rx3_remote.cli listen
```

Replay a captured tuple by name:

```sh
python -m tools.rx3_remote.cli command deck.play_pause \
  --channel 1 --operation 2 --value 0 --float-value 0
```

Move the browser selector one detent by supplying the same signed step in both
value fields:

```sh
python -m tools.rx3_remote.cli command browser.rotary_selector \
  --channel 0 --operation relative_moved --value 1 --float-value 1
```

Buttons and verified 10-bit absolute controls have shorter forms:

```sh
python -m tools.rx3_remote.cli press deck.play_pause --channel 1
python -m tools.rx3_remote.cli set mixer.crossfader 512 --channel 0
```

`press` sends the firmware's pressed and released operations in order. `set`
sends operation 5 and derives its float value as `raw / 1023`.

The recovered operations are pressed (0), long pressed (1), released (2),
released after long press (3), relative moved (4), absolute moved (5), and
value changed (6). The raw `command` form preserves every field for captures
whose control-specific value semantics still need labeling.

Every frame starts with a 16-byte network-order header. Payloads are fixed-size
binary records; the device performs no JSON parsing. A command has a nonzero
request ID, and the matching ACK or ERROR repeats it. A connection begins with
HELLO and SCHEMA records. SCHEMA carries the CRC-32 and count of
`controls.json`, allowing clients and the module to detect mismatched maps.

`controls.json` is the source of truth for human-readable names. Regenerate the
C constants after changing it:

```sh
python -m tools.rx3_remote.generate_header
```

Unknown key codes remain valid in incoming events and are displayed as
`key_0xNNNN`. Commands intentionally require a known name or known numeric code
until its scope and value shape have been reviewed.
