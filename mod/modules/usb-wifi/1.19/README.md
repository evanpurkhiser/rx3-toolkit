<!-- SPDX-License-Identifier: MPL-2.0 -->
# USB Wi-Fi

This firmware 1.19 module connects the RX3 directly to a 2.4 GHz Wi-Fi
network. It turns the supported USB Wi-Fi adapter into the `eth0` interface
that the stock player application already uses for Link Export.

## Supported hardware

The validated adapter is an Edimax RTL8188CU device with USB ID `7392:7811`.
Both values must match. The runtime finds the adapter through sysfs and refuses
to continue when it finds zero or multiple matching interfaces.

The module packages the complete stack for that device:

| Layer | Packaged components |
| --- | --- |
| Kernel | `compat-average`, `cfg80211`, `mac80211`, `rtlwifi`, `rtl8192c-common`, `rtl8192cu` |
| Firmware | `rtlwifi/rtl8192cufw.bin` and its redistribution license |
| Userspace | Static ARM `wpa_supplicant`, `wpa_cli`, and `rx3-ifrename` |

The packaged license directory contains the Linux GPL-2.0, libnl LGPL-2.1,
wpa_supplicant BSD, and musl MIT terms. The Realtek firmware license is next to
the firmware payload.

The build procedures are in
[`tools/rx3_usb_wifi_kernel`](../../../../tools/rx3_usb_wifi_kernel) and
[`tools/rx3_usb_wifi_userspace`](../../../../tools/rx3_usb_wifi_userspace).
Those documents identify which component belongs to each layer and how to
rebuild every checked-in artifact in offline Podman containers.

Other RTL8188CU adapters are not enabled implicitly. A different USB ID needs
an explicit runtime match and device validation. A different chipset needs a
separate driver and firmware payload.

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

The module copies this file into mode-0600 RAM before starting the supplicant.
It does not print the configuration or passphrase to the session log. Edit the
file on the toolkit drive to change networks.

## Runtime sequence

On a cold boot or normal toolkit-drive insertion, the module:

1. installs the Realtek firmware and loads the six kernel modules in dependency
   order;
2. finds exactly one `7392:7811` interface;
3. copies the userspace programs and Wi-Fi configuration into `/dev/shm`;
4. associates while the adapter still has its kernel-assigned name, normally
   `wlan0`;
5. requests a player-application restart;
6. after the old application exits, renames rear USB-B from `eth0` to `usbB0`
   and the Wi-Fi adapter from `wlan0` to `eth0`;
7. restarts association on `eth0`, then launches the unmodified player
   application;
8. lets the application run its stock DHCP client, followed by one bounded
   `udhcpc -i eth0 -T 2 -t 3 -n -q` recovery attempt when needed.

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

Build the toolkit with `usb-wifi`. For diagnosis, also select `logging` and a
recovery-shell module. Put the Wi-Fi configuration at the path above, then
boot with the toolkit drive in one USB-A port and the supported adapter in the
other. A full power cycle is the clearest test because kernel modules and
interface names live only in RAM.

Do not manually run the autoexec payload from an SSH session carried by the
Wi-Fi interface being renamed. Taking that interface down terminates the
session and can kill its shell-owned process group. Use the player's USB
hotplug path or cold boot. Rear USB-B remains available as `usbB0`, retaining
its addresses and routes across the rename. A diagnostic listener bound to all
addresses, such as the optional Telnet module, remains reachable there.

With logging enabled, inspect `RX3_RUNTIME/session.txt` on the toolkit drive
and `/tmp/rx3-usb-wifi.state` on the player. Success reports `carrier=1`, a
normal non-link-local IPv4 address, and either `dhcp=rbp-bound` or
`dhcp=recovery-bound`.

## Validation and limitations

The driver and userspace stack completed nl80211 scanning, WPA2/CCMP
association, and DHCP on the RX3. With a 72 Mbit/s link at roughly -85 dBm,
iperf 3 measured 14.5 Mbit/s from RX3 to LAN and 19.8 Mbit/s from LAN to RX3.
The six wireless modules occupy 508,024 bytes and `wpa_supplicant` used about
640 KiB RSS in that session.

Only firmware 1.19 and USB ID `7392:7811` are guarded. The adapter is 2.4 GHz
802.11n hardware. Enterprise EAP, WPS, roaming services, NetworkManager, and a
configuration UI are outside this module. Link Export discovery and database
traffic still depend on the network's multicast/broadcast policy and routing.
