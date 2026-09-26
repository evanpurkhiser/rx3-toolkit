<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 kernel module builds

This directory owns the toolchain and ABI checks shared by optional kernel
modules. Feature-specific Kconfig and Kbuild steps live in
`tools/rx3_<module>_kernel`.

A prepared kernel source tree must contain the production `.config`,
`Module.symvers`, `include/config/kernel.release`, and matching kernel source.
The builder copies that tree into an isolated container, lets the selected
feature configure and build its modules, then rejects outputs with the wrong
architecture, vermagic, modversion table, or position-independent relocations.

Build the common toolchain image once:

```sh
make kernel-builder
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
- any original compatibility source or Kbuild files the feature owns.

The recipe sources `/tool/kernel-build-lib.sh`, changes Kconfig as needed, calls
`rx3_kernel_prepare`, and builds through `rx3_kernel_make`. The common runner
provides the firmware release and production symbol table and validates every
filename declared by `modules.list`.
