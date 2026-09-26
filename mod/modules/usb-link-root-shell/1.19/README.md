<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB Link root shell

This firmware 1.19 module runs BusyBox `telnetd -F -l /bin/sh` on TCP port 23.
Each connection opens `/bin/sh` directly as root, with no username or password.
It uses the RX3's existing Link Export network over the rear USB-B computer
port and leaves the stock USB gadget in place. Before starting the daemon, it
adds `169.254.100.2/16` as the `eth0:rx3shell` secondary address. The primary
Link Export address remains intact, so the firmware can continue managing it.
The module pins the server peer `169.254.100.1/32` to `eth0`. This keeps shell
replies on rear USB-B if the S3-side `usb0` simultaneously acquires a competing
`169.254/16` AutoIP route.

The toolkit keeps the foreground daemon's process ID in `/tmp`. Reinserting the
drive during the same boot recognizes the owned listener and leaves it running.
An unrelated service already listening on port 23 prevents this module from
starting.

> [!WARNING]
> Connect the RX3 directly to the Mac with a USB cable. Do not enable this
> module while the Link Export interface can reach a shared network, a venue
> network, or another computer. Anyone who can reach TCP port 23 receives an
> unencrypted, unauthenticated root shell.

The regular **Telnet access** module runs the firmware's login program and
requires the root password. This module deliberately bypasses that login for
bench development. The two Telnet modules and the USB serial shell cannot be
selected together.

## Connect from macOS

1. Build the toolkit drive with **USB Link root shell** selected, connect the
   Mac directly to the RX3's rear USB-B computer port, and insert the toolkit
   drive in the player.
2. Give Link Export time to appear. macOS normally gives its side an Automatic
   Private IP address in `169.254.0.0/16`. The RX3 shell is always available at
   `169.254.100.2`, including after the rear cable is reconnected.
3. Connect with a Telnet client:

   ```sh
   telnet 169.254.100.2 23
   ```

   Current macOS releases may not include a Telnet client. A Homebrew Telnet
   client works; `nc 169.254.100.2 23` is a basic fallback but does not perform
   terminal negotiation.

If macOS does not assign an Automatic Private address, identify the direct USB
network interface and add a host address without replacing any existing one:

```sh
ifconfig | awk '/^[a-z0-9]+:/{i=$1; sub(":", "", i)} /status: active/{print i}'
sudo ifconfig enX inet 169.254.100.1 netmask 255.255.0.0 alias
telnet 169.254.100.2 23
```

Replace `enX` with the rear USB-B network interface. The fixed addresses assume
a direct, isolated cable; disconnect another device using either address before
testing.

There is no login prompt. A shell prompt means the direct root session is
ready. Run `id` to verify that it reports UID 0, and use `exit` to close the
session.

## Recovery

The daemon, its process ID file, and all toolkit changes live in RAM. Removing
the toolkit drive while the player is running does not stop the daemon. To
return to stock state, power the RX3 off, remove the toolkit drive, and power it
on again. If session logging is also selected, eject its filesystem cleanly
before removing the drive.
