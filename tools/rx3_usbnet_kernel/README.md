# RX3 1.19 USB CDC-NCM host modules

This directory contains reproducible external-module builds for the two host
drivers needed to recognize the ESP32-S3 Link Export bridge on the RX3's top
USB-A port:

- `usbnet.ko` provides the generic Linux USB networking framework.
- `cdc_ncm.ko` binds a CDC-NCM communications interface and depends on
  `usbnet.ko`.

Firmware 1.19 already builds the required USB host controller, networking,
MII, CRC32, and power-management facilities into its kernel. No additional
loadable dependency is required.

Build against the prepared production kernel tree:

```sh
./tools/rx3_usbnet_kernel/build-modules.sh \
  /home/evan/workspace/rx3-research/usb-serial-build/kernel-source \
  /tmp/rx3-usbnet-output
```

The script copies the untouched Pioneer 3.0.101 driver sources into a temporary
external-module directory. It stages GCC 12.2 from the existing
`localhost/rx3-usb-serial-builder:bookworm` image, then builds and validates the
modules inside `localhost/rx3-reverse:latest`. Both rootless Podman containers
run with networking disabled. The source tree is mounted read-only.

The build refuses a source tree that does not match release
`3.0.101-2790-gc248ed7-svn3098`, lacks symbol versioning, or lacks a required
built-in dependency. It also checks each module's vermagic, dependency
metadata, CDC-NCM USB class alias, and `__versions` section.

The checked-in modules have these properties:

```text
usbnet.ko
  SHA-256  d85ffb61ccdf34d76e67575da34c0c6d414aa3c25a7e3a91cccd05dfea891ea8
  format   ELF 32-bit LSB relocatable, ARM EABI5
  vermagic 3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
  depends  none

cdc_ncm.ko
  SHA-256  14246e71514ce8c966cb5267fe082c5fa9bd2487697a844778f3946bbed60e95
  format   ELF 32-bit LSB relocatable, ARM EABI5
  vermagic 3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
  depends  usbnet
  alias    USB communications class 0x02, NCM subclass 0x0d, protocol 0x00
```

Load the framework before the class driver:

```sh
insmod /path/to/usbnet.ko
insmod /path/to/cdc_ncm.ko
```

After attaching the S3, verify the bind before changing interface names:

```sh
dmesg | tail -n 40
cat /proc/net/dev
```

The modules have been build-validated against the recovered production kernel
inputs. Loading and traffic validation require an RX3 running firmware 1.19.
