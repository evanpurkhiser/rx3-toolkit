<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB Link root shell

This firmware 1.19 module runs BusyBox `telnetd -F -l /bin/sh` on TCP port 23.
Each connection opens `/bin/sh` directly as root, with no username or password.
It uses the RX3's existing Link Export network over the rear USB-B computer
port and leaves the stock USB gadget in place.

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
2. Give Link Export time to appear. macOS and the RX3 choose Automatic Private
   IP addresses in `169.254.0.0/16`; there is no DHCP server and the addresses
   can change after either end reconnects.
3. Find the Mac interface and the RX3 address:

   ```sh
   ifconfig | awk '/^[a-z0-9]+:/{i=$1; sub(":", "", i)} /inet 169\.254\./{print i, $2}'
   arp -an | grep '169\.254\.'
   ```

   The first command identifies the Mac's Link Export interface. In the ARP
   output, use the other `169.254.x.y` address, associated with that interface.
   If it has not appeared yet, unplug and reconnect the USB-B cable, wait for
   the interface to return, and run `arp -an` again.
4. Connect with a Telnet client:

   ```sh
   telnet 169.254.x.y 23
   ```

   Current macOS releases may not include a Telnet client. A Homebrew Telnet
   client works; `nc 169.254.x.y 23` is a basic fallback but does not perform
   terminal negotiation.

There is no login prompt. A shell prompt means the direct root session is
ready. Run `id` to verify that it reports UID 0, and use `exit` to close the
session.

## Recovery

The daemon, its process ID file, and all toolkit changes live in RAM. Removing
the toolkit drive while the player is running does not stop the daemon. To
return to stock state, power the RX3 off, remove the toolkit drive, and power it
on again. If session logging is also selected, eject its filesystem cleanly
before removing the drive.
