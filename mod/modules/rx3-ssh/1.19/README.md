<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 authenticated SSH

This firmware 1.19 module runs Dropbear 2026.94 as an authenticated root shell
on TCP port 22. It listens on the RX3's network addresses, including the fixed
NCM management address and the LAN address leased through the ESP32-S3 bridge.
The module requires `s3-link-bridge` and leaves the rear USB-B interface,
routes, and `rbp` untouched.

The module can run alongside `usb-link-root-shell`. SSH listens on port 22,
while that module keeps an unauthenticated recovery shell on port 23 at the
USB-B-only `169.254.100.2` address. Development payloads should include both so
an SSH configuration or S3 bridge failure does not remove recovery access.

Password and PAM authentication are disabled at compile time. The build also
disables TCP, Unix-socket, agent, and X11 forwarding. It supports Ed25519 user
and host keys, modern OpenSSH key exchange, ChaCha20-Poly1305, and AES-CTR.
Port 22 must be unused; the module refuses to replace an existing listener.
The toolkit's decrypted ISO records regular module files as mode 0644, so the
S3 FAT volume stages `RX3_SSH/dropbearmulti`. The module verifies its pinned
SHA-1, copies the 296,936-byte executable into `/dev/shm/rx3-ssh`, verifies the
RAM copy, and creates `dropbear` and `dropbearkey` multicall symlinks there.
Dropbear changes its working directory to `/` before launch, so it can keep
running while the decrypted image and S3 media are unmounted or rescanned.

## Provision access

Create this file on the toolkit drive before inserting it into the RX3:

```text
RX3_SSH/authorized_keys
```

The S3 firmware staging task installs the matching server binary beside it:

```text
RX3_SSH/dropbearmulti
```

It must contain one or more plain `ssh-ed25519` public-key lines. Private keys
never belong on the drive or in this module. At runtime the module copies the
public keys into RAM at `/root/.ssh/authorized_keys`, owned by root with mode
0600. The stock `/root` directory is group-writable, so the module removes that
write bit in RAM to satisfy Dropbear's authorized-key ownership checks.

The RX3 generates an Ed25519 host key into `/tmp`. When the toolkit drive is
writable, it persists the key as:

```text
RX3_SSH/dropbear_ed25519_host_key
```

Later boots copy that identity into `/tmp` with mode 0600 before Dropbear reads
it. If the drive is read-only, SSH still starts with a volatile key and reports
that its identity will change at reboot. The module payload contains no private
keys.

Connect through the management address or the DHCP lease:

```sh
ssh root@172.31.254.2
ssh root@RX3_LAN_ADDRESS
```

Runtime state and logs live at `/tmp/rx3-ssh.state` and `/tmp/rx3-ssh.log`.
The state file records the public host-key fingerprint and whether the identity
was loaded, persisted, or left volatile.

## Reproducible build

The source archive and SHA-256 are pinned in `tools/rx3_dropbear/build.sh`.
Compilation runs offline in the existing Podman ARM toolchain:

```sh
tools/rx3_dropbear/build.sh
```

The build compiles musl 1.2.5 and Dropbear entirely inside the Podman container,
then produces a static ARMv7 EABI5 soft-float executable. Both source archives
and their SHA-256 digests are pinned. Function and data sections, linker garbage
collection, and `-Os` for libtommath reduce the stripped binary from 990,340
bytes with static glibc to 296,936 bytes with static musl, a 70.0% reduction.
The current artifact is:

```text
d0f9410f8dbcdb39198b7c7194626e56071ba13d2bd1b903b0a9a3b6de40f3f6  dropbearmulti
```

The smaller libc does not remove protocol support. The binary retains Ed25519,
Curve25519, group14-SHA256, the SNTRUP761 and ML-KEM 768 hybrid exchanges,
ChaCha20-Poly1305, and AES-128/256-CTR. Passwords, PAM, forwarding, X11, and
re-exec remain disabled. Run the offline QEMU smoke test after building:

```sh
tools/rx3_dropbear/test.sh
```

The smoke test checks the ELF ABI and static linkage, inventories the required
algorithms, generates an Ed25519 host key through the ARM `dropbearkey` entry
point, starts the ARM server under QEMU, and completes an Ed25519-authenticated
OpenSSH command. musl supports Linux 2.6.39 and newer, which includes the RX3's
Linux 3.0.101 kernel. QEMU cannot reproduce the exact kernel and firmware
userspace, so a live execution check remains required whenever the compiler or
libc changes.

The current artifact was staged on the S3 FAT volume, copied to tmpfs, and
executed there on RX3 firmware 1.19. Its `dropbearkey` entry point generated and
read an Ed25519 host key, the daemon accepted the configured Personal Ed25519
key over the S3 NCM bridge, and a remote command completed as UID 0. The RAM
executable uses 296,936 bytes, the idle daemon reported 208 KiB RSS, and the
authenticated shell reported 512 KiB RSS.

The build disables Dropbear's re-exec and privilege-switch paths. A small
RX3-specific patch bypasses `initgroups()` only when both the daemon and
authenticated account are already UID/GID 0. This keeps the root-only service
independent of libc name-service behavior.
