<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 Link bridge

This firmware 1.19 module loads the RX3 kernel's `usbnet.ko` and `cdc_ncm.ko`
host drivers from `autoexec.bin`. It identifies the S3 by its complete USB
identity and requires exactly one network interface beneath that USB device:

```text
VID      303a
PID      4012
product  RX3 Wi-Fi Link Bridge
name     usb0
```

The kernel interface names stay unchanged. Rear USB-B remains `eth0`, and the
S3 CDC-NCM interface remains `usb0`.

## Application routing

RX3 1.19 `rbp` contains a shared null-terminated `eth0` string at ELF file
offset `0x003f6678` (virtual address `0x003fe678`). Its application network
manager, address listener, PC-control address lookup, and network-change monitor
reference this string. The stock DHCP command embeds another `eth0` at file
offset `0x004d955a` inside `udhcpc -i eth0 -T 2 -t 3 -n -q`. The module
replaces the shared four-byte word directly. Because the DHCP name starts two
bytes off a four-byte boundary, two adjacent aligned guarded words spanning
`i eth0 -` produce `i usb0 -`. This preserves the runtime's normalized hash,
rollback, and safe-reinsertion checks.

The runtime verifies both the complete stock `rbp` SHA-1 and the four bytes at
the patch site before writing. The modified executable lives in RAM, and the
change disappears at power-off. If the S3 identity, `usb0`, or carrier is
missing, the prepare hook fails and the runtime writes no patch.

The replacement `rbp` runs its stock DHCP path on `usb0`. The module does not
start another DHCP client. After launch it adds `usb0:mgmt` at
`172.31.254.2/30` for recovery without replacing the DHCP lease.

Runtime status is stored in:

```text
/tmp/rx3-s3-link-bridge.state
```

The module also carries an ARM status and recovery utility:

```sh
/mnt/iso/modules/s3-link-bridge/rx3-ncm-status usb0
/mnt/iso/modules/s3-link-bridge/rx3-ncm-status usb0 bootloader
```

Select `s3-link-activation` with this module to invoke the stock PC-mounted
transition after the replacement `rbp` starts. `usb-link-root-shell` keeps
Telnet available over rear USB-B, while `rx3-ssh` provides authenticated access
over the S3 network path.
