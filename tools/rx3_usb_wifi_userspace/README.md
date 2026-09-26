<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 USB Wi-Fi userspace

This directory builds the hardware-independent userspace half of the
`usb-wifi` module:

| Component | Purpose |
| --- | --- |
| `wpa_supplicant` | Associates through nl80211 using the drive configuration |
| `wpa_cli` | Provides local control and diagnostics |
| `rx3-ifrename` | Renames interfaces with the kernel's rtnetlink API |
| `rtlwifi/rtl8192cufw.bin` | Firmware loaded by the RTL8188CU driver |
| `LICENCE.rtlwifi_firmware.txt` | Firmware redistribution terms |

The executables are static ARMv7 musl binaries. The supplicant includes the
nl80211 driver, file configuration backend, control socket, and internal WPA
crypto, without unrelated EAP methods or service frameworks.

## Build

Fetch the pinned, checksum-verified sources and build the toolchain image:

```sh
tools/rx3_usb_wifi_userspace/fetch-sources.sh \
  /tmp/rx3-usb-wifi-sources

podman build \
  -t localhost/rx3-usb-wifi-builder:bookworm \
  tools/rx3_usb_wifi_userspace
```

The actual build runs with networking disabled:

```sh
tools/rx3_usb_wifi_userspace/build.sh \
  /tmp/rx3-usb-wifi-sources \
  /tmp/rx3-usb-wifi-userspace
```

After reviewing the printed SHA-256 values, stage the outputs:

```sh
cp /tmp/rx3-usb-wifi-userspace/wpa_supplicant \
   /tmp/rx3-usb-wifi-userspace/wpa_cli \
   /tmp/rx3-usb-wifi-userspace/rx3-ifrename \
   mod/modules/usb-wifi/1.19/
cp /tmp/rx3-usb-wifi-userspace/LICENCE.rtlwifi_firmware.txt \
   mod/modules/usb-wifi/1.19/
cp /tmp/rx3-usb-wifi-userspace/rtlwifi/rtl8192cufw.bin \
   mod/modules/usb-wifi/1.19/rtlwifi/rtl8192cufw.fw
cp /tmp/rx3-usb-wifi-userspace/licenses/* \
   mod/modules/usb-wifi/1.19/licenses/
```

The firmware file is specific to the RTL8192CU/RTL8188CU family. A module for
another chipset must package the firmware requested by that driver's
`request_firmware()` call.

The staged license texts cover wpa_supplicant (BSD), libnl (LGPL-2.1), and
musl (MIT). The runtime manifest carries them into the toolkit image alongside
the executables.
