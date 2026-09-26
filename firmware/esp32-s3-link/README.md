# RX3 ESP32-S3 Link bridge

This project is the first firmware proof of concept for attaching a Seeed Studio
XIAO ESP32-S3 to an RX3 USB-A source port and carrying Link Export over the
existing Wi-Fi network.

The firmware exposes a composite USB device with CDC-NCM Ethernet and a
read-only mass-storage disk. It is a single-client MAC-sharing bridge based on
Espressif's official `tusb_ncm` example. The S3 uses
one MAC address for its Wi-Fi station and NCM interface, then forwards complete
Ethernet frames in both directions. It does not run an IP stack, DHCP server,
DHCP client, NAT, or Pro DJ Link translator. The RX3's own DHCP client should
receive a lease from the real LAN router through the bridge.

Wi-Fi receive callbacks copy frames into a bounded static pool and release the
radio driver's buffer immediately. A core-1 worker submits those copies to
TinyUSB asynchronously. This keeps the Wi-Fi driver responsive when the RX3
temporarily disables or re-enumerates NCM during an application restart; USB
backpressure drops a bounded number of frames instead of wedging the bridge.

The RX3 owns that LAN lease; the S3 does not independently acquire a management
address in the transparent mode. This keeps rekordbox's LAN device identity
consistent in UDP discovery and the TCP dbserver. The server proxy proved that
mixing a translated USB identity with an untranslated LAN dbserver identity
causes the RX3 to reject the library connection. RX3 static analysis shows that
the dbserver client compares the response with the peer ID learned earlier; it
does not require USB identity `0x11`. A consistent LAN identity such as `0x29`
therefore follows the stock success path without S3 packet translation.

The final 5 MiB of the XIAO's 8 MiB flash is a wear-levelled FAT volume
containing `autoexec.bin`. The RX3 can load its bootstrap and NCM kernel modules
from the same physical device that subsequently carries Link traffic. The
volume must accept writes because firmware 1.19 mounts every VFAT USB source
with `-o rw` before invoking `decrypt_autoexec.sh`; a write-protected disk is
visible in the SOURCE UI but its bootstrap never executes.

The composite descriptor places NCM before MSC. Firmware 1.19's
`CONFIG_PDJ` USB-core patch stops registering interfaces immediately after a
mass-storage interface, so Espressif's default MSC-first order exposes the disk
but silently omits NCM. With NCM first and MSC last, the RX3 registers all three
interfaces before reaching that stop condition.

## Pinned environment

- ESP-IDF `v6.1`
- `espressif/esp_tinyusb` `2.0.1~1`
- Target `esp32s3`

The bridge uses Espressif's private raw Wi-Fi APIs. Exact version pins and the
committed `dependencies.lock` keep that interface and transitive components
reproducible. Generated SDK and toolchain directories stay local to this
project and are ignored by Git.

Install the tools declared by `mise.toml`, then install the repo-local ESP-IDF
checkout and cross compiler:

```sh
cd firmware/esp32-s3-link
mise install
mise run setup
```

Create the ignored local configuration and enter the Wi-Fi credentials:

```sh
mise run configure
$EDITOR sdkconfig.local.defaults
```

Build and run host-side validation:

```sh
mise run test
mise run build
```

`mise run build` stages `../../build/autoexec.bin` into the generated FAT image.
Set `AUTOEXEC=/absolute/path/to/autoexec.bin` to use another runtime image. The
file remains ignored and the firmware build never reads or embeds an AES key.

The build command is the step that downloads the pinned managed component on a
new checkout. `mise run check` runs validation and the firmware build together.

## Flashing the XIAO

Connect the XIAO directly to the development machine and run:

```sh
PORT=/dev/ttyACM0 mise run flash
```

The normal flash task writes the bootloader, partition table, application, and
bootstrap disk. Later payload changes can update only the disk partition:

```sh
AUTOEXEC=/path/to/autoexec.bin PORT=/dev/ttyACM0 mise run flash-disk
```

The fixed flash layout is:

| Range | Size | Contents |
| --- | ---: | --- |
| `0x000000–0x2fffff` | 3 MiB | bootloader, NVS, partition table, application |
| `0x300000–0x7fffff` | 5 MiB | wear-levelled FAT bootstrap disk |

The exact device path varies. Hold **BOOT**, tap **RESET**, then release **BOOT**
to force the ROM download mode if the running TinyUSB firmware prevents normal
flashing. The XIAO's USB-C connector is wired to the ESP32-S3 native USB PHY on
GPIO19 (D-) and GPIO20 (D+); those pins must remain unused by the application.

TinyUSB NCM owns the same internal PHY used by USB Serial/JTAG while the
application runs. Runtime logs therefore use UART0. Attach a 3.3 V USB-UART
adapter with crossed signals:

| XIAO pin | UART adapter |
| --- | --- |
| GPIO43 / TX | RX |
| GPIO44 / RX | TX |
| GND | GND |

Then run `PORT=/dev/ttyUSB0 mise run monitor`. Do not connect a 5 V UART signal
to the XIAO pins. The board is bus-powered by the RX3 in the intended setup, so
the firmware does not configure self-powered USB or a separate VBUS-sense GPIO.

## USB identity and Wi-Fi configuration

The product string and serial string are fixed in `sdkconfig.defaults`. The
composite device uses PID `0x4012`; this differs from the earlier NCM-only PID
because USB hosts cache interface layouts by VID/PID. The prototype uses
Espressif's VID with a project-specific PID; this is suitable for
private development, but a distributed product needs an allocated USB VID/PID.

By default the bridge uses the chip's stable factory Wi-Fi station MAC for both
Wi-Fi and NCM. `CONFIG_RX3_LINK_WIFI_MAC` can override it with a locally
administered unicast address. The firmware rejects multicast and globally
administered overrides. Keep a chosen override stable because the router's DHCP
reservation and the RX3 interface identity depend on it.

Credentials live only in ignored `sdkconfig.local.defaults` and the ignored
generated `sdkconfig`. Never add either file to Git.

The RX3 can replace those fallback credentials through the versioned NCM
configuration protocol. The S3 stores an SSID/password pair as one NVS blob,
reports live association, RSSI, NCM carrier, SSID, and the shared adapter MAC,
and never returns the password. See
[`docs/configuration-protocol.md`](docs/configuration-protocol.md) for the wire
format and failure semantics.

## RX3 proof-of-concept procedure

Start with the RX3 bootstrap module disabled so this test isolates kernel USB
enumeration and Ethernet behavior.

1. Reserve a LAN address for the bridge MAC in the router's DHCP configuration.
2. Connect UART logging and power the XIAO from a computer. Confirm that the log
   reports `USB NCM carrier up`, then records the Wi-Fi association separately.
3. Disconnect power, attach the XIAO USB-C port to an RX3 top USB-A source port,
   and boot the RX3 with its normal debug/telnet module available separately.
4. On the RX3, record `dmesg -w`, `ip -d link`, and `lsusb -v`. Confirm that a
   CDC-NCM function enumerates and identify its kernel interface name.
5. Bring that interface up without renaming it. Capture with
   `tcpdump -eni <interface> 'arp or (udp port 67 or 68)'`, then run
   `udhcpc -f -v -i <interface>`. The DHCP discover, offer, request, and ACK
   must cross the S3, and the lease must come from the LAN router.
6. Ping the router and a LAN host from the RX3. From another LAN host, ping the
   RX3's new lease. Confirm ARP uses the shared bridge MAC.
7. Capture simultaneously on the RX3 NCM interface and a LAN host while sending
   fragmented UDP in both directions. Use a payload matching observed Link
   traffic, including 8,292-byte datagrams, and verify byte-identical reassembly
   with no increasing drop counter.
8. Sustain bidirectional traffic while disconnecting and reconnecting Wi-Fi.
   NCM carrier remains up so the configuration API can recover invalid
   credentials. Transparent LAN frames are rejected until association returns;
   the RX3 integration renews DHCP after a successful reconnect.
9. Load an exported track while watching the ten-second statistics line. Record
   throughput and `wifi_drop`/`usb_drop` before tuning NCM NTB sizes or counts.

The periodic statistics are cumulative fixed-width counters. No frame history
is retained. `usb_drop` counts RX3-to-Wi-Fi frames rejected while disconnected
or by the Wi-Fi driver. `wifi_drop` counts Wi-Fi-to-RX3 frames that cannot enter
the bounded TinyUSB NCM transmit path or are rejected while USB is unavailable.
`tx_ok` and `tx_fail` report the Wi-Fi driver's physical transmit completion,
while `last_tx` only reports whether the driver accepted the frame for queuing.
The `RX3STAT?` response also includes the associated access point's `rssi`, with
`-127` indicating that no current association record was available.

Keep the kernel interface names unchanged. Once raw DHCP and fragmented UDP
pass, the RX3 autoexec shim can guard and patch `rbp`'s shared interface literal
from `eth0` to `usb0`, restart the application, and invoke the stock
mounted/certification path described in the main S3 plan.

## References

- [ESP-IDF v6.1 TinyUSB NCM example](https://github.com/espressif/esp-idf/tree/v6.1/examples/peripherals/usb/device/tusb_ncm)
- [Espressif USB Device Stack](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)
- [Seeed XIAO ESP32-S3 documentation](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
- [Seeed XIAO ESP32-S3 schematic](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/XIAO_ESP32S3_V1.3_SCH_260115.pdf)
