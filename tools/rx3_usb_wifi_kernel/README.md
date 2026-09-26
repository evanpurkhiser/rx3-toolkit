<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 1.19 RTL8188CU kernel modules

This directory builds the wireless kernel stack used by the `usb-wifi`
runtime module. The validated adapter is an Edimax USB device with ID
`7392:7811`; it contains an RTL8188CU and is handled by `rtl8192cu` in the
Linux 3.0.101 source published for RX3 firmware 1.19.

The production kernel already supplies USB host support, firmware loading,
crypto, LEDs, module loading, and symbol versioning. Build and package these
six modules, in this order:

| Component | Purpose |
| --- | --- |
| `compat-average.ko` | Exports the two EWMA helpers expected by mac80211 |
| `cfg80211.ko` | Linux wireless configuration API and regulatory support |
| `mac80211.ko` | Soft-MAC 802.11 stack |
| `rtlwifi.ko` | Shared Realtek driver code |
| `rtl8192c-common.ko` | Shared RTL8192C/RTL8188C implementation |
| `rtl8192cu.ko` | USB driver containing the `7392:7811` device ID |

Wireless Extensions stay disabled so enabling WLAN does not change the
conditional layout of `struct net_device`. Userspace controls the interface
through nl80211.

## Build

Fetch the manufacturer's [published GPL source package](https://www.pioneerdj.com/en/support/open-source-code-distribution/gnu-open-source-license/).
The common fetcher verifies both published ZIP files and their reconstructed
source archive:

```sh
make kernel-source FIRMWARE=1.19
```

The published symbol table predates firmware 1.19. `production.symvers` carries
only the production CRC entries imported by these six outputs, authenticated
by `production.symvers.sha256`. The common builder verifies that the profile
has no missing or unused symbols and refuses any release other than
`3.0.101-2790-gc248ed7-svn3098`.

Build the shared pinned toolchain image once, then run the feature recipe
offline through the common kernel-module builder:

```sh
make kernel-builder
make kernel-modules \
  MODULE=usb-wifi \
  FIRMWARE=1.19 \
  KERNEL_SOURCE=build/kernel-source/1.19
```

The source tree is mounted read-only and copied into an isolated temporary
tree. The builder restores the production symbol CRCs after
`modules_prepare`, then validates every result's ARM vermagic, modversion
table, position-independent-code constraints, and pinned SHA-256 digest. The
resulting `.ko` files live under `build/artifacts/1.19/usb-wifi/`, where the
runtime packager finds them. They are generated artifacts and stay out of Git.

Other RTL8188CU device IDs may exist in the same driver's table, but the
runtime deliberately accepts only `7392:7811`. Supporting another ID requires
changing the runtime's vendor/product match and validating its USB topology,
association, suspend behavior, and throughput on the RX3. A different Wi-Fi
chipset requires its own vendor kernel module and firmware; do not select or
rename one of these six files for it.
