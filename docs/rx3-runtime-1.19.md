<!-- SPDX-License-Identifier: MPL-2.0 -->
# RX3 firmware 1.19 runtime anatomy

This document records a read-only probe of a running XDJ-RX3 on firmware
`1.19`. The observations came from `/proc`, `/sys`, device nodes, boot scripts,
and the live `rbp` process through the USB Link root shell. Values that describe
one boot, such as process IDs and automatically selected IP addresses, are
examples rather than stable ABI.

## Platform

| Component | Observed value |
| --- | --- |
| SoC | Freescale i.MX6, four ARM Cortex-A9 cores (`CPU part 0xc09`) |
| Architecture | ARMv7 with Thumb, VFPv3, and NEON |
| RAM | 1,017,100 KiB available to Linux; no swap |
| Kernel | `3.0.101-2790-gc248ed7-svn3098`, SMP and preemptible |
| Kernel build | GCC 4.6.2, built May 24, 2024 |
| Userspace | BusyBox 1.20.2 and glibc 2.13 |
| Firmware revision | `1.19 [3101:3100]` |
| Platform source revision | SVN revision 3101, changed April 11, 2024 |
| LTIB metadata | App version `9.1.1`, built May 24, 2024 |
| Display | 1280×800 at 60 Hz, RGB565 |

The machine boots with `isolcpus=3`. The sampled `rbp` leader was restricted to
CPU 0 while display, DMA, USB, Ethernet, and timer interrupts were distributed
across the other cores. The real-time clock was not set: this boot began at
January 1, 2021 even though the kernel and firmware were built in 2024.

## Boot and recovery surfaces

BusyBox `init` runs `/etc/rc.d/rcS`. The last normal boot step launches
`/root/pdj/apl_start`, which starts the player application. A second respawning
init entry starts a serial login console on `ttymxc0` at 115200 baud. The kernel
command line contains both:

```text
console=ttymxc0,115200 jtag=on
```

This confirms that the board has an enabled physical UART console and an
enabled JTAG boot setting. Physical access and board-level pin identification
are still required to use either one.

The root filesystem is RAM-backed. `/tmp` and `/var` are separate 512 KiB
tmpfs mounts. Settings and GUI assets are writable UBIFS volumes:

| Mount | Backing store | Capacity during probe |
| --- | --- | ---: |
| `/root/settings` | `ubi6:settings` | 6.6 MiB |
| `/root/gui` | `ubi10:gui` | 33.4 MiB |
| `/media/usb1/sda1` | Toolkit drive EFI partition | 196.9 MiB |
| `/media/usb1/sda2` | Toolkit drive data partition | 29.0 GiB |
| `/mnt/iso` | Decrypted toolkit image through `/dev/loop0` | 394 KiB |

The toolkit ISO remains mounted read-only at `/mnt/iso` after its startup script
runs. The shell, network alias, PID files, and loaded code live in RAM. The UBI
volumes are persistent and should be treated as firmware state rather than
scratch space.

The 256 MiB NAND has seventeen overlapping logical partitions. The named
firmware regions are:

| Name | Size |
| --- | ---: |
| `boot` | 3 MiB |
| `bb1` | 1 MiB |
| `env` | 1 MiB |
| `kernel_a` | 10 MiB |
| `kernel_b` | 10 MiB |
| `rootfs` | 30 MiB |
| `settings` | 10 MiB |
| `pdj` | 8 MiB |
| `bb2` | 5 MiB |
| `core` | 30 MiB |
| `gui` | 40 MiB |
| `quickboot` | 102 MiB |
| second `bb2` entry | 5 MiB |
| `bbt` | 1 MiB |

The command line also defines aggregate `block_boot`, `block_linux`, and
`block_all` regions. The duplicate `bb2` name comes directly from the firmware
partition table.

## Rear USB-B architecture

The rear computer connection is three devices behind an internal two-port USB
hub:

```text
rear USB-B
└── Pioneer hub                         2b73:003e
    ├── USB 10/100 LAN                  2b73:0007
    └── XDJ-RX3 audio/MIDI/HID gadget   2b73:003d
```

The LAN adapter presents MAC `…:9a` to the computer. The RX3 side is the i.MX6
FEC Ethernet controller, `eth0`, with adjacent MAC `…:99`. This explains why
USB Link networking remains available independently of the player composite
gadget.

The `g_pmulti` kernel module implements the other branch. Its production build
provides audio, MIDI, and HID functions. The running module has these notable
parameters:

| Parameter | Value |
| --- | ---: |
| `audio_buf_size` | 2048 |
| `req_buf_size` | 1024 |
| `req_count` | 8 |
| `hidg_qlen` | 4 |
| `qlen` | 4 |
| `notify_delay_msec` | 1000 |

Only `g_pmulti` and FUSE were loadable modules during the probe; most hardware
drivers are built into the kernel. Replacing `g_pmulti` affects audio, MIDI,
and HID enumeration but does not remove the sibling USB Ethernet adapter.

The fixed shell module added `169.254.100.2/16` as `eth0:rx3shell`. It preserved
the stock AutoIP address, which was `169.254.175.153` during this boot. The
computer used `169.254.100.1/16`.

## USB-A host side

The RX3 has two built-in EHCI host controllers. During this probe, the toolkit
drive appeared on bus 2 through a high-speed hub:

```text
Freescale EHCI host
└── hub 0451:8027
    └── SanDisk mass-storage device 0781:5580
```

The kernel includes USB mass storage, USB HID, and USB audio host support. It
does not include USB CDC ACM or USB network host drivers. A Wi-Fi or serial
peripheral connected to USB-A therefore needs an exact-kernel module unless it
uses one of the built-in classes.

Relevant production configuration:

```text
CONFIG_USB_STORAGE=y
CONFIG_USB_HID=y
CONFIG_SND_USB_AUDIO=y
# CONFIG_USB_ACM is not set
# CONFIG_USB_USBNET is not set
# CONFIG_USB_SERIAL is not set
```

## Main player process

Nearly all product behavior lives in one unstripped executable:

| Property | Observed value |
| --- | --- |
| Path | `/root/pdj/rbp` |
| SHA-1 | `cf309238491e73cdbdc1f08a09f7a3177e079068` |
| User | root, with all capabilities |
| Threads | 76 |
| Resident memory | about 295 MiB |
| Virtual address space | about 930 MiB |
| Open-file limit | 1024 soft, 4096 hard |

The mapped graphics stack is DirectFB 1.4 on the Vivante framebuffer driver.
Audio decoding libraries include FLAC and mpg123. Other notable mappings are
ALSA, JPEG, g2d, and the Vivante `libGAL-fb` implementation.

Thread names expose useful subsystem boundaries:

| Area | Examples |
| --- | --- |
| Playback | `Player:0`, `Player:1`, `PlayerPrevew`, `WavWriter` |
| Track state | `Tot_MInfoRenewT`, `Tot_TrackSearch`, `Tot_AutoCueLoad` |
| UI | `Ui_CycleTask`, `Ui_EventTask`, `UiMain`, `gui_task` |
| Database | `DB_Client`, `DB_Trans1`, `DBSA_Task_USB`, `DBMC_TASK_USB` |
| Network | `NetworkManager`, `Autoip`, `autoip_recv_arp`, `UDP_server` |
| Controls | `AsyncHidDataHan`, `Pioneer HID Inp`, `Pioneer HID Out` |
| Hardware | `JogCom:1`, `JogCom:2`, `TouchPanel`, `GpioManager` |
| Audio | `JuceALSA`, `Juce MIDI Input`, `BpmWaveDetectMa` |

The process directly holds descriptors for:

- `/dev/fb0`, `/dev/galcore`, and `/dev/mem`;
- `/dev/gpiodrv` through many independent descriptors;
- two plain SPI controller channels and two ready-signalled SPI channels;
- `/dev/tsc2007_2-0048` for the touch panel;
- all master, headphone, booth, microphone, AUX, and input PCM endpoints;
- `/dev/hidg0` for both input and output;
- the USB hotplug pseudo-files under `/proc/udev_*`;
- `export.pdb` and `exportExt.pdb` from the mounted Rekordbox drive.

This arrangement is favorable for event-driven telemetry. Track commits, player
state, UI events, database transitions, and outgoing HID reports all occur
inside `rbp`; a hook can publish state at those transitions without periodically
sampling the whole process.

## Controls and internal buses

The normal Linux input subsystem was empty. Surface controls are handled by
custom character devices instead:

```text
/dev/gpiodrv
/dev/subucom_spi1.0
/dev/subucom_spi2.0
/dev/subucom_spi_rdy3.0
/dev/subucom_spi_rdy4.0
/dev/tsc2007_2-0048
```

The four SPI links have dedicated kernel threads. The two `JogCom` application
threads strongly suggest that at least two of these links carry deck/jog
controller traffic. This is an inference from names and ownership; the live
probe did not read or interfere with the character-device streams.

## Audio

ALSA reports three cards:

| Card | Purpose |
| --- | --- |
| 0, `esai-cs4344-audio` | AUX capture |
| 1, `cs4344-audio-rev8` | Internal mixer I/O |
| 2, `g_pmidi` | USB MIDI gadget |

Card 1 exposes three playback and three capture pairs:

| PCM | Playback | Capture |
| --- | --- | --- |
| `01-00` | Master out | Input 2 |
| `01-01` | Headphone out | Microphone input |
| `01-02` | Booth out | Input 1 |

The kernel contains product-specific PCM1791A and CS4344 drivers. A virtual
`paudiog0` device connects `rbp` to the USB audio gadget.

## Graphics

The display stack uses two i.MX IPUs and the Vivante GPU. Linux exposes:

| Framebuffer | Geometry | Purpose/name |
| --- | --- | --- |
| `fb0` | 1280×800, 16 bpp | `DISP3 BG`, active main display |
| `fb1` | 240×960 virtual, 16 bpp | `DISP3 FG` |
| `fb2` | 1280×800, 16 bpp | `DISP3 BG - DI1` |

The kernel reserves `0x3f000000–0x3fffffff` for the Vivante `galcore` driver.
`rbp` maps `/dev/fb0`, `/dev/galcore`, and `/dev/mem` directly.

## Network services

The sampled socket table contained:

| Protocol | Binding | Interpretation |
| --- | --- | --- |
| TCP | `0.0.0.0:12523` | Stock service; consistent with Pro DJ Link dbserver |
| TCP | `0.0.0.0:23` | Toolkit root shell |
| UDP | `0.0.0.0:50000` | Pro DJ Link discovery |
| UDP | `127.0.0.1:20000` | Internal loopback service |
| UDP | ephemeral port 41076 | Application-owned socket |

Ports 50001 and 50002 were not open with no active Pro DJ Link peer. They may be
created on demand as link state changes.

## Kernel development properties

The production kernel is unusually convenient for experimental modules:

```text
CONFIG_MODULES=y
CONFIG_MODULE_UNLOAD=y
CONFIG_MODULE_FORCE_UNLOAD=y
CONFIG_MODVERSIONS=y
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y
CONFIG_DEBUG_FS=y
CONFIG_PREEMPT=y
CONFIG_SMP=y
```

The kernel does not carry DWARF debug information, but the complete kallsyms
table and versioned exports make exact-kernel external modules practical. The
running `g_pmulti` reports the expected production vermagic:

```text
3.0.101-2790-gc248ed7-svn3098 SMP preempt mod_unload modversions ARMv7
```

With the network root shell active, an experimental `.ko` or userspace helper
can be transferred into RAM, loaded, inspected, and removed without rebuilding
the toolkit image for every iteration. `/dev/shm` has far more working space
than the 512 KiB `/tmp` mount. Persistent UBI volumes should not be used for
iteration.

## Reproducing the inventory

The useful read-only commands are available in the stock firmware:

```sh
uname -a
cat /proc/cpuinfo /proc/meminfo /proc/cmdline /proc/mtd
df -h
mount
ps -eLf

p=$(pidof rbp)
cat /proc/$p/status
cat /proc/$p/maps
ls -la /proc/$p/fd
for task in /proc/$p/task/*; do
    printf '%s ' "${task##*/}"
    cat "$task/comm"
done

ifconfig -a
route -n
cat /proc/net/tcp /proc/net/udp
cat /proc/modules
zcat /proc/config.gz

cat /proc/asound/cards /proc/asound/devices /proc/asound/pcm
cat /proc/bus/input/devices
fbset
```

`/proc/<pid>/fd` changes with loaded media and network state. Record descriptor
targets without opening or reading the underlying character devices.

## Probe boundaries

The probe read files, metadata, descriptors, and kernel state. It did not read
control streams, audio samples, track metadata, raw NAND, or persistent setting
contents. It did not write internal flash or alter the running application.
Device serial numbers are omitted from this document.
