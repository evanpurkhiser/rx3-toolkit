<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 USB Wi-Fi hardware profiles

The `usb-wifi` runtime is independent of a particular adapter. Each directory
under `profiles/` supplies the kernel recipe for one validated hardware model
and revision. The matching declarative runtime data lives under
`mod/modules/usb-wifi/profiles/` with the same directory name.

No profile is selected by default. Available profiles are:

| Profile | Adapter | Chipset | USB ID | State |
| --- | --- | --- | --- | --- |
| `edimax-ew-7811un-v1` | Edimax EW-7811Un V1 | RTL8188CU | `7392:7811` | RX3 validated |
| `tp-link-archer-t3u-v1` | TP-Link Archer T3U V1 | RTL8812BU | `2357:012d` | RX3 live validated; cold boot pending |

Select by the adapter's exact USB ID and revision. Use the Edimax profile for
the fully validated packaged cold-boot path. Use the TP-Link profile for its
higher measured throughput when live validation is sufficient; packaged
cold-boot validation is still pending. A profile cannot be substituted for a
different adapter even when the devices share a marketed name or desired link
speed.

Build a profile explicitly:

```sh
make kernel-builder
make kernel-source FIRMWARE=1.19
make kernel-modules \
  MODULE=usb-wifi \
  PROFILE=tp-link-archer-t3u-v1 \
  FIRMWARE=1.19 \
  KERNEL_SOURCE=build/kernel-source/1.19
```

Use the same slug when packaging the runtime:

```sh
make autoexec MODULES="usb-wifi" \
  PROFILES="usb-wifi=tp-link-archer-t3u-v1" \
  KEY=/path/to/aes256.key
```

The output is written to
`build/artifacts/1.19/usb-wifi/tp-link-archer-t3u-v1/`. The generic kernel
target resolves this module's recipe from the selected `PROFILE`.

## Profile contents

The runtime profile contains:

| File | Purpose |
| --- | --- |
| `profile.json` | Name, revision, chipset, USB IDs, and supplicant backend |
| `usb.ids` | USB vendor/product pairs accepted at runtime |
| `modules.load` | Kernel module names, files, and dependency order |
| `firmware.list` | Packaged source path to `/lib/firmware` destination mappings |
| `supplicant.driver` | Backend passed to `wpa_supplicant -D` |
| `artifacts.list` | Profile-owned firmware and license files to package |
| `sources.sha256` | Digests for downloaded profile-owned inputs |

The kernel recipe with the same profile name contains `build.sh`,
`modules.list`, and the production symbol-version profile. Out-of-tree drivers
also carry a pinned source manifest, a fetcher, and a preparation script. The
common kernel recipe runner downloads those sources into
`build/sources/usb-wifi/`, prepares a temporary recipe, then cross-compiles
without network access in Docker.

## Identifying an adapter

Identify the USB device itself rather than relying on the product listing or
case label. Vendors frequently change chipsets between hardware revisions.

On a Linux host, connect only the candidate adapter and run:

```sh
lsusb -nn
udevadm info --query=property --path="$(udevadm info --query=path --name=wlan0)"
```

Record the four-digit vendor and product values shown as `vvvv:pppp`, the
marketed model, and every printed revision. Determine the active host driver
and its aliases with:

```sh
readlink -f /sys/class/net/wlan0/device/driver
ethtool -i wlan0
modinfo DRIVER_NAME | grep '^alias:'
```

The driver used by a modern host may not exist in the RX3's Linux 3.0.101
tree. Search that prepared source tree for the USB pair and inspect the
driver's USB device table:

```sh
rg -i '0xVVVV.*0xPPPP|0xPPPP.*0xVVVV' build/kernel-source/1.19
rg 'MODULE_DEVICE_TABLE\(usb' build/kernel-source/1.19/drivers/net/wireless
```

Then determine:

1. the leaf driver and every module it depends on;
2. their required Kconfig settings and load order;
3. firmware paths passed to `request_firmware()` and the licenses governing
   those blobs;
4. whether the driver supports nl80211, since the packaged supplicant is built
   for that backend;
5. every production-kernel symbol used by the resulting modules.

The common kernel builder validates ARM EABI, production vermagic,
`CONFIG_MODVERSIONS`, required symbol CRCs, and pinned module hashes in an
offline Docker container. A successful compile establishes binary
compatibility; it does not establish hardware support.

Use symbol CRCs captured from a running production RX3. The published 1.19
kernel `Module.symvers` differs from the device kernel and produces modules
that the loader rejects at `module_layout`.

## Adding a profile

Copy an existing pair of profile directories and give both the same specific
slug, including the hardware revision. Update all declarative files and write
the driver-specific kernel recipe. Keep common userspace programs out of the
driver recipe.

Validate on an RX3 1.19 cold boot before describing a profile as supported:

- discovery matches exactly the intended USB ID;
- firmware and every module load without kernel errors;
- scanning, WPA association, and DHCP complete;
- unplug/replug and one USB reset recover cleanly;
- rear USB-B remains reachable after the interface swap;
- sustained throughput, CPU use, and memory use are recorded.

Adapters with the same chipset but a different USB ID still need a distinct
validated profile or an intentional additional ID in an existing profile.
