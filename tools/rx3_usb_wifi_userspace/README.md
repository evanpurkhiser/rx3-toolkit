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

Fetch the pinned, checksum-verified source archives, firmware, and firmware
licence from their upstream projects:

```sh
tools/rx3_usb_wifi_userspace/fetch-sources.sh \
  /tmp/rx3-usb-wifi-sources
```

`sources.sha256` verifies every download before the container starts. The
build also requires the downloaded firmware licence to match the notice
packaged by the runtime module.

Build the toolchain image:

```sh
podman build \
  -t localhost/rx3-usb-wifi-builder:bookworm \
  tools/rx3_usb_wifi_userspace
```

The actual build runs with networking disabled:

```sh
tools/rx3_usb_wifi_userspace/build.sh \
  /tmp/rx3-usb-wifi-sources
```

The default output is `build/artifacts/1.19/usb-wifi/`, alongside the kernel
artifacts consumed by the runtime packager. Pass a second argument to build
elsewhere. `artifacts.sha256` checks every result against the validated build;
a compiler, dependency, or input change therefore fails instead of silently
changing the packaged payload.

The firmware file is specific to the RTL8192CU/RTL8188CU family. A module for
another chipset must package the firmware requested by that driver's
`request_firmware()` call.

The tracked license texts cover the Realtek firmware, wpa_supplicant (BSD),
libnl (LGPL-2.1), and musl (MIT). The runtime manifest carries them into the
toolkit image alongside the generated artifacts.
