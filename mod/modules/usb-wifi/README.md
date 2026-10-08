<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB Wi-Fi

This firmware 1.19 module connects the RX3 directly to a Wi-Fi network. It
turns the selected, supported USB Wi-Fi adapter into the `eth0` interface
that the stock player application already uses for Link Export.

## Supported hardware

Hardware support is selected through an explicit build profile. There is no
default adapter. The available profiles are `edimax-ew-7811un-v1`, for an
Edimax RTL8188CU device with USB ID `7392:7811`, and
`tp-link-archer-t3u-v1`, for a TP-Link RTL8812BU device with USB ID
`2357:012d`. The runtime finds IDs declared by the selected profile through
sysfs and refuses to continue when it finds zero or multiple matching
interfaces.

The module packages the complete stack for that device:

| Layer | Packaged components |
| --- | --- |
| Kernel | `compat-average`, `cfg80211`, `mac80211`, `rtlwifi`, `rtl8192c-common`, `rtl8192cu` |
| Firmware | `rtlwifi/rtl8192cufw.bin` and its redistribution license |
| Userspace | Static ARM `wpa_supplicant`, `wpa_cli`, and `rx3-netctl` |

The packaged license directory contains the Linux GPL-2.0, libnl LGPL-2.1,
wpa_supplicant BSD, and musl MIT terms. The Realtek firmware license is next to
the firmware payload.

The build procedures are in
[`tools/rx3_usb_wifi_kernel`](../../../tools/rx3_usb_wifi_kernel) and
[`tools/rx3_usb_wifi_userspace`](../../../tools/rx3_usb_wifi_userspace).
Those documents identify which component belongs to each layer and how to
build the generated artifacts in offline Docker containers. All outputs live
under `build/artifacts/1.19/usb-wifi/<profile>/` and remain outside Git.

Other adapters are not enabled implicitly. A different model or revision needs
a validated profile with its USB IDs, kernel recipe, firmware, module order,
and supplicant backend. The hardware-profile guide explains how to identify a
chipset and add support.

Choose the profile by the adapter's exact USB ID and hardware revision:

- Use `edimax-ew-7811un-v1` for USB ID `7392:7811`. Its complete packaged
  cold-boot path is validated on an RX3.
- Use `tp-link-archer-t3u-v1` for USB ID `2357:012d`. It provides substantially
  higher measured throughput and has been live-tested on an RX3, while a full
  cold-boot test of the packaged path remains pending.

The profile must match the connected hardware; it is not a performance preset.
Pass the same profile to the kernel build and final toolkit build.

## Configuration

Create this file on the toolkit drive:

```text
RX3_WIFI/wpa_supplicant.conf
```

Start from `modules/usb-wifi/wpa_supplicant.conf.example` in a built toolkit
image. A minimal WPA2 personal configuration is:

```text
ctrl_interface=/dev/shm/rx3-usb-wifi/control
update_config=0
network={
    ssid="network name"
    psk="network passphrase"
}
```

The module copies this file into mode-0600 RAM before starting the supplicant
and sets `ctrl_interface` in that copy to its private runtime directory. It
does not print the configuration or passphrase to the session log. Edit the
file on the toolkit drive to change networks.

## Runtime sequence

On a cold boot or normal toolkit-drive insertion, the module:

1. reads the packaged hardware profile, installs its firmware, and loads its
   kernel modules in declared dependency order;
2. finds exactly one interface matching a declared USB ID;
3. copies the userspace programs and Wi-Fi configuration into `/dev/shm`;
4. associates from supplicant control events while the adapter still has its
   kernel-assigned name, normally `wlan0`;
   if the RTL8192CU stack remains stuck scanning, resets that USB device once,
   rediscovers its interface name, and retries association;
5. requests a player-application restart;
6. after the old application exits, renames rear USB-B from `eth0` to `usb0`
   and the Wi-Fi adapter from `wlan0` to `eth0`;
   assigns `169.254.100.2/16` to `usb0` so management services remain
   reachable whenever the rear cable has carrier;
7. starts association on `eth0` and immediately launches the unmodified player
   application;
8. completes the in-progress association after launch, then lets the
   application run its stock DHCP client and observes address changes through
   route netlink, followed by one bounded
   `udhcpc -i eth0 -T 2 -t 3 -n -q` recovery attempt when needed.

Reinsertion after a successful swap reuses the associated `eth0` connection.
If a USB reset returns the adapter as `wlan0` while rear USB-B remains
`usb0`, the stopped hook restores the Wi-Fi adapter to `eth0` before launch.

The player application, its cached interface index, DHCP command, and Link
paths therefore keep using their stock `eth0` assumptions. This module does
not patch network bytes and does not activate Link Export; select the separate
`link-export-activate` module when that behavior is wanted.

If a stopped hook, binary write, replacement launch, or readiness check fails,
the runtime stops its supplicant and reverses the interface swap before
restoring the previous application. If the Wi-Fi interface cannot be renamed
back, it unbinds only the detected USB adapter so rear USB-B can reclaim
`eth0`.

## Build and cold-boot use

Build the kernel and userspace artifacts using the procedures linked above,
then build the toolkit with an explicit profile:

```sh
make autoexec MODULES="usb-wifi" \
  PROFILES="usb-wifi=edimax-ew-7811un-v1" \
  KEY=/path/to/aes256.key
```

For diagnosis, also select `logging`
and a recovery-shell module. Put the Wi-Fi configuration at the path above,
then boot with the toolkit drive in one USB-A port and the supported adapter
in the other. A full power cycle is the clearest test because kernel modules
and interface names live only in RAM.

Do not manually run the autoexec payload from an SSH session carried by the
Wi-Fi interface being renamed. Taking that interface down terminates the
session and can kill its shell-owned process group. Use the player's USB
hotplug path or cold boot. Rear USB-B remains available as `usb0` at
`169.254.100.2/16`. The address stays configured while unplugged and becomes
reachable when the cable has carrier, so it requires no polling process. A
diagnostic listener bound to all addresses, such as the optional Telnet module,
remains reachable there.

With logging enabled, inspect `RX3_RUNTIME/session.txt` on the toolkit drive,
`/tmp/rx3-usb-wifi.state`, and `/tmp/rx3-usb-wifi.log` on the player. Success
reports `carrier=1`, a normal non-link-local IPv4 address, and either
`association=connected` with either `dhcp=rbp-bound` or
`dhcp=recovery-bound`.

## Validation and limitations

The driver and userspace stack completed nl80211 scanning, WPA2/CCMP
association, and DHCP on the RX3. With a 72 Mbit/s link at roughly -85 dBm,
iperf 3 measured 14.5 Mbit/s from RX3 to LAN and 19.8 Mbit/s from LAN to RX3.
The six wireless modules occupy 508,024 bytes and `wpa_supplicant` used about
640 KiB RSS in that session.

The TP-Link Archer T3U V1 profile loaded on production firmware 1.19,
associated over 5 GHz at an 867 Mbit/s PHY rate and roughly -37 dBm, obtained
a DHCP lease, and remained reachable through rear USB-B. A single iperf 3
stream measured 82.4 Mbit/s from RX3 to LAN and 150 Mbit/s from LAN to RX3.
Two streams measured 80.2 and 125 Mbit/s respectively, so parallel streams did
not improve throughput. Total system CPU busy time rose from a 34% player
baseline to 62--64% during sustained traffic, while about 573 MiB of RAM
remained free. Association and DHCP also recovered after a physical adapter
replug when userspace was restarted. Cold-boot validation of the packaged
runtime remains pending.

Only firmware 1.19 and the IDs in the selected profile are guarded. The
validated Edimax profile is 2.4 GHz 802.11n hardware. Enterprise EAP, WPS,
roaming services, NetworkManager, and a
configuration UI are outside this module. Link Export discovery and database
traffic still depend on the network's multicast/broadcast policy and routing.
