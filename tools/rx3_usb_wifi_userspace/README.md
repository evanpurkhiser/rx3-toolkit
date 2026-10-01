<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 USB Wi-Fi userspace

This directory builds the hardware-independent ARM userspace used by every
`usb-wifi` profile: static `wpa_supplicant`, `wpa_cli`, and `rx3-ifrename`.
The selected profile supplies device firmware, licenses, USB IDs, kernel
modules, and the supplicant backend.

Choose a container runtime, then build the common source inputs and one
explicit profile with:

```sh
DOCKER=podman

"$DOCKER" build \
  --file tools/rx3_usb_wifi_userspace/Containerfile \
  --tag rx3-usb-wifi-builder:bookworm \
  tools/rx3_usb_wifi_userspace

tools/rx3_usb_wifi_userspace/fetch-sources.sh build/usb-wifi-sources
mod/modules/usb-wifi/profiles/edimax-ew-7811un-v1/fetch-sources.sh \
  build/usb-wifi-sources
DOCKER="$DOCKER" tools/rx3_usb_wifi_userspace/build.sh \
  edimax-ew-7811un-v1 build/usb-wifi-sources
```

Use `DOCKER=docker` for Docker. The build adds Podman's keep-ID user namespace
when that runtime is selected, so its unprivileged container can write the
bind-mounted artifact directory without changing host permissions.

The build runs offline after source retrieval, verifies every common and
profile-owned input, and writes to
`build/artifacts/1.19/usb-wifi/<profile>/`. It refuses an omitted or unknown
profile. Run the matching kernel build into the same directory before
packaging the runtime.

`wpa_supplicant` currently includes nl80211 only. A hardware profile that
needs another backend must first extend this common build and declare that
backend in its `supplicant.driver` file.
