<!-- SPDX-License-Identifier: MPL-2.0 -->
# Bidirectional remote control

This firmware 1.19 module exposes the RX3's high-level panel dispatcher on TCP
port 7357. A client can subscribe to physical control events and submit the
same seven-field tuple that the front-panel receivers pass to
`uif::IKeyManager::sendKey`. The module listens on every configured network
interface and leaves interface setup to the selected transport module.

The module is statically composed into the shared runtime and enabled through
the `remote-control` selection. The core input service owns the guarded
`sendKey` hook. Remote control registers an observer that copies each complete
native payload into a nonblocking local datagram. Socket parsing, framing,
acknowledgements, and command injection run on a worker thread.

The worker dispatches commands through the shared input service. Module key
handlers can consume an event; otherwise the service forwards it to the
firmware exactly once. The firmware hands off worker-thread calls to its UI
thread, and a pending marker suppresses duplicate remote telemetry on replay.

The server allows one client. It begins each connection with `HELLO` and
`SCHEMA`, accepts `SUBSCRIBE`, `COMMAND`, and `PING`, and reports physical and
remote events with a monotonic device timestamp. An event flag of 1 means at
least one earlier callback event could not enter the bounded queue. Commands
are rejected until a real firmware call has supplied an initialized key
manager. Power, USB-stop, calibration, and test controls cannot be injected.

Build the shared runtime and run the source tests with:

```sh
make hook test
```

Use `python -m tools.rx3_remote.cli --help` for the host client. The ready and
diagnostic files are `/tmp/rx3-remote-control.ready` and
`/tmp/rx3-remote-control.log` on the player.

The protocol does not authenticate clients. Use it on a directly attached or
otherwise trusted network, and pass the RX3 address with `--host` when it is
not available at the client's default USB Link address.

Restarting `rbp` while a drive remains inserted loses the stock udev mount
notification even though the filesystem stays mounted. A separate replay
worker waits for the new `UsbMountManager` to open `/proc/udev_usb1`, scans the
current `/media/usb1/*` mounts, and selects the single partition containing
`PIONEER/rekordbox/export.pdb`. It then makes one write attempt containing
`mount <path>` with no trailing newline. A toolkit/runtime partition is never
announced because it does not contain the Rekordbox database.

The replay uses its own worker, separate from the remote-control server, and
both workers are joined when the module stops. The firmware 1.19 proc-node
driver can block an additional writer on its semaphore, so recovery makes one
bounded write attempt. Do not retry the notification, write both partitions,
trigger the block device's uevent, or read `/proc/udev_usb1` from a shell. Those
operations can duplicate an app-level mount, rerun toolkit startup, or consume
the notification intended for `UsbMountManager`.
