<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 kernel module builds

This directory owns the toolchain and ABI checks shared by optional kernel
modules. Feature-specific Kconfig and Kbuild steps live in
`tools/rx3_<module>_kernel`.

A published kernel source tree must contain the production `.config`,
`include/config/kernel.release`, and matching source. The builder copies that
tree into an isolated container, prepares it for the production release, and
then rejects outputs with the wrong architecture, vermagic, modversion table,
or position-independent relocations.

Build the common toolchain image once:

```sh
make kernel-builder
```

Fetch Pioneer's checksum-pinned GPL source release:

```sh
make kernel-source FIRMWARE=1.19
```

Build one module's artifacts:

```sh
make kernel-modules \
  MODULE=example \
  FIRMWARE=1.19 \
  KERNEL_SOURCE=/path/to/prepared/kernel
```

Outputs go to `build/artifacts/<firmware>/<module>/`. They are ignored by Git.
A runtime manifest marks a generated input with `"artifact": true`; packaging
then reads it from that directory instead of the module's source directory.

## Feature recipe contract

`tools/rx3_<module>_kernel` contains:

- `build.sh`, executed inside the offline container with the copied kernel tree
  as its first argument and an empty output directory as its second;
- `modules.list`, one output `.ko` filename per line;
- `production.symvers`, the minimal production CRC entries imported by those
  outputs, and `production.symvers.sha256`, which authenticates that profile;
- any original compatibility source or Kbuild files the feature owns.

The recipe sources `/tool/kernel-build-lib.sh`, changes Kconfig as needed, calls
`rx3_kernel_prepare`, and builds through `rx3_kernel_make`. The common runner
provides the firmware release, validates every filename declared by
`modules.list`, and proves that the minimal production symbol profile exactly
covers the built modules' external versioned imports.
