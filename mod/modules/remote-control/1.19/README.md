<!-- SPDX-License-Identifier: MPL-2.0 -->
# Bidirectional remote control prototype

This firmware 1.19 module exposes the RX3's high-level panel dispatcher on TCP
port 7357 over the USB Link Ethernet interface. It is independent of the audio
and video streamers. A client can subscribe to physical control events and can
submit the same seven-field tuple that the front-panel receivers pass to
`uif::IKeyManager::sendKey`.

The preload accepts only the verified firmware 1.19 `rbp` SHA-1
`cf309238491e73cdbdc1f08a09f7a3177e079068`. It verifies the eight-byte
`sendKey` prologue before patching. The hook copies one fixed-size event to a
nonblocking local datagram and immediately enters the original trampoline.
Socket parsing, framing, acknowledgements, and command injection run on a
worker thread.

`sendKey` already supports callers outside the UI thread. It packages the
arguments in a firmware-owned message and invokes the same virtual method on
the UI thread. The worker enters the hook so the remote event is available
immediately, and the hook calls its original trampoline exactly once. A
pending marker suppresses the later UI-thread replay. The module does not
synthesize a private `KeyInput` object or call a downstream player handler.

The server allows one client. It begins each connection with `HELLO` and
`SCHEMA`, accepts `SUBSCRIBE`, `COMMAND`, and `PING`, and reports physical and
remote events with a monotonic device timestamp. An event flag of 1 means at
least one earlier callback event could not enter the bounded queue. Commands
are rejected until a real firmware call has supplied an initialized key
manager. Power, USB-stop, calibration, and test controls cannot be injected.

Build the shared object in the offline ARM container:

```sh
tools/rx3_remote/build-device.sh /tmp/rx3-remote-build
```

Use `python -m tools.rx3_remote.cli --help` for the host client. The ready and
diagnostic files are `/tmp/rx3-remote-control.ready` and
`/tmp/rx3-remote-control.log` on the player.
