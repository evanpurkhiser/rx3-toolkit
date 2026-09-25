<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 USB-A Link Export plan

## Objective

An ESP32-S3 plugged into an RX3 top USB-A host port should join an existing
Wi-Fi network and make the RX3 appear to rekordbox as a normal Link Export
device. The RX3 should browse and load the remote rekordbox library without a
computer connected to its rear USB-B port.

The first version targets one rekordbox computer and one RX3. PCM transport is
reserved in the USB design but remains a separate milestone.

## Proposed topology

```text
rekordbox on Wi-Fi
        |
        | Pro DJ Link, dbserver, RPC, mountd, NFS
        v
ESP32-S3 Wi-Fi station
  - one-host Ethernet-over-Wi-Fi forwarder
  - one shared USB/Wi-Fi MAC address
  - USB NCM or ECM device
        |
        | USB-A, RX3 is host
        v
RX3 USB network interface
        |
        v
rbp stock Link Export implementation
```

Espressif's official `tusb_ncm` example implements the preferred data path. It
assigns the USB NCM function the Wi-Fi station's MAC, transmits complete USB
Ethernet frames through the Wi-Fi driver, and forwards received Wi-Fi frames
back to USB. The USB host obtains an address directly from the LAN router.

The S3 associates at layer 2 but does not acquire its own IPv4 address on that
shared MAC. The RX3 owns the LAN lease. A management web server on the same
identity would compete with forwarded RX3 traffic, so initial diagnostics use
UART, endpoint-zero control requests, or counters retrieved through an
explicitly designed management channel.

The S3 does not run a DHCP server in this mode. It forwards the RX3's DHCP
broadcasts to the access point and returns the router's replies. The RX3 image
contains `udhcpc`, and `rbp` contains an `udhcpc -i eth0` command path. The
autoexec bootstrap should explicitly start `udhcpc` on the replacement `eth0`
after NCM carrier rises, then wait for a non-link-local address before entering
the mounted Link state. If no lease arrives, it should leave Link unavailable
and retry rather than accepting the stock `169.254/16` fallback as a usable
LAN address.

This works within a Wi-Fi station's three-address constraint because exactly
one downstream host, the RX3, uses the station's permitted MAC. It is not an
arbitrary multi-MAC bridge. The RX3 should therefore appear directly on the LAN
at its own DHCP or static IPv4 address, with Pro DJ Link payloads unchanged.

The lease belongs to the RX3 in this transparent design. The S3 supplies the
shared layer-2 path and normally has no separate IP stack or management
address. If the S3 instead acquires `10.0.0.x` for itself and leaves the RX3 on
a private or link-local subnet behind it, that becomes the routed proxy design
and still requires address and identity translation.

## Implication of the successful server relay

The server proxy proved that the RX3 rejected a mixed identity, rather than
requiring device `0x11` unconditionally. The translated UDP session presented
rekordbox as USB device `0x11`, while the first dbserver response still named
its LAN device `0x29`; the RX3 closed that TCP connection before requesting a
menu. Rewriting the response to `0x11` made live folder and track-list browsing
work.

Transparent S3 forwarding should remove the mismatch at its source. The RX3
will announce its real DHCP address and shared NCM/Wi-Fi MAC. Rekordbox can
remain LAN device `0x29`, and its dbserver will report the same `0x29` identity.
Firmware 1.19's SOURCE builder explicitly accepts PC device IDs `0x29` through
`0x2c`, so the first S3 implementation should carry the protocol unchanged:

- no IP or MAC rewriting;
- no UDP `0x29` to `0x11` normalization;
- no TCP dbserver broker;
- no NAT, RPC proxy, mountd proxy, or NFS awareness.

This is a testable expectation, not an assumption that USB attachment is
irrelevant. Capture both sides during the first Link attempt. If they show a
mixed identity, normalize the relevant UDP identity and the first dbserver
response together. Translating only one side recreates the proven failure.

The existing routed translator remains the fallback if `rbp` accepts only the
rear-USB rekordbox personality. In that design the S3 terminates each side,
translates the captured IP, MAC, and device-ID fields on UDP 50000-50002, and
routes the remaining unicast traffic.

## RX3 USB host support

Firmware 1.19 has USB mass-storage, HID, and audio host support built in, but
its production config disables USB networking. Pioneer's released 3.0.101
source contains the required drivers:

- `usbnet`
- `cdc_ether`
- `cdc_ncm`
- `rndis_host`

Build exact-kernel modules with the recovered production config,
`Module.symvers`, release string, and ARM toolchain. Start with NCM because
Espressif provides an S3 Wi-Fi-to-USB NCM example and NCM can aggregate several
Ethernet frames into one USB transfer. Keep ECM as the compatibility fallback
for the old kernel.

The first hardware test is deliberately network-only:

1. Load `usbnet.ko` and `cdc_ncm.ko` on the RX3.
2. Attach an NCM-only S3 firmware image.
3. Give NCM the S3 Wi-Fi station MAC and run Espressif's raw frame-forwarding
   path.
4. Keep NCM carrier down until Wi-Fi association completes, then run `udhcpc`
   on RX3 `eth0` or apply the reserved RX3 LAN address.
5. Confirm enumeration, carrier, LAN addressing, broadcast delivery, repeated
   unplug/replug, and sustained bidirectional traffic.
6. Measure packet loss and throughput with 64-byte, 1500-byte, and fragmented
   UDP traffic before involving `rbp`.

Mass storage and audio should not be included in this first descriptor. Each
class is added only after the simpler descriptor works reliably with the RX3's
old USB host stack.

The XIAO ESP32-S3 USB-C connector is wired to the S3 native USB peripheral and
uses device-role pull-downs on its configuration-channel pins. A normal
USB-A-to-C cable therefore makes it a bus-powered peripheral of the RX3; it
does not need to source VBUS.

The S3 USB peripheral is Full Speed. Espressif reports roughly 6.4 Mbit/s for
ordinary TinyUSB transfers, or about 0.8 MB/s before Wi-Fi and forwarding
overhead. Track loading will be slower than the rear 100 Mbit/s adapter, but
this should be enough for Link Export if control traffic receives priority.
Measure the real ceiling before allocating bandwidth to PCM.

## Interface selection

The built-in `eth0` is the RX3 i.MX6 FEC connection to the rear USB-LAN
hardware. Static analysis confirms that `rbp` hardcodes one `eth0` string at
`0x003fe678` across its network stack. The direct users include:

- `get_myip_info` at `0x00188b9c`;
- `PcControlMacAddress::getIpAddress` at `0x002e9ca8`;
- `MacAddressListener::run` at `0x00340e40`;
- `NetworkManager` MAC, address, and netmask helpers from `0x003912bc` through
  `0x003919f0`;
- `NetworkMonitor::checkNetworkConnectionChange` at `0x003920a4`.

`MacAddressListener` caches `if_nametoindex("eth0")` when its thread starts.
`NetworkManager::initialize` also reads the interface MAC once and supplies it
to Pro DJ Link self-information before normal operation. The adapter name and
MAC must therefore be final before `rbp` launches.

Use the toolkit's stopped-application hook to perform a deterministic swap:

1. Match the built-in FEC by its known MAC/topology and rename `eth0` to
   `eth1`.
2. Load `usbnet` and the selected NCM/ECM module.
3. Match the S3 interface by USB VID/PID, serial, and topology.
4. Bring both interfaces down, rename the S3 interface to `eth0`, and bring it
   up.
5. Obtain or apply the RX3 LAN address, then launch `rbp`.

This aligns every stock code path without interposing broad libc
`ioctl`/`if_nametoindex` calls or patching many internal helpers. Enumeration
order is never a naming input.

If the S3 is absent at boot, keep the rear FEC as `eth0` and launch the stock
path. If the active S3 disconnects, a supervisor can stop `rbp`, restore
`eth1` to `eth0`, and relaunch. Hot-switching interfaces underneath a
running `rbp` is unsafe because its listener retains the original ifindex and
Pro DJ Link retains the original self MAC.

## RX3 application bootstrap

The stock rear connection combines a USB-mounted callback with a PC-control
certificate. The current preload already invokes
`PcController::handleUsbMountMessage`, which initializes the mixer key map,
starts MIDI reception, and sets the mounted flag observed by `NetworkMonitor`.

Static analysis separates the two gates. `NetworkMonitor` reads the mounted
byte at `PcController+0x72`; its Link state does not call
`PcController::isCertified`. The stock mount callback at `0x002e9700` handles
both transitions: event 3 sets the mounted byte, initializes mixer key data,
and starts MIDI reception, while event 4 clears the byte and stops reception.

`PcControlCert::rcvActivateCmd` at `0x00367e8c` starts the one-second lease used
by the captured MIDI heartbeat. `PcControlCert::checkCertStatus` at
`0x00367e1c` sets the same active state and invokes its callback without
starting the expiry timer. The S3 path can therefore establish adjacent
PC-control state once through stock code without synthesizing MIDI.

For USB-A operation, a carrier-aware shim should drive the same stock state
transitions:

1. Wait for the S3 network interface to enumerate and report carrier.
2. Ensure the S3 owns `eth0`, then start `rbp` so it captures the correct
   ifindex and MAC.
3. Wait for a valid LAN address from DHCP, or apply the reserved address.
4. Invoke the stock mounted callback with event 3.
5. Call `PcControlCert::checkCertStatus` once if the adjacent PC-control UI
   needs certification.
6. On disconnect, stop `rbp`, invoke event 4, restore the rear FEC as `eth0`,
   and relaunch.

This keeps the application in its native state machine. Only add periodic
certification reassertion if runtime evidence shows another stock transition
clearing it. An unconditional byte patch would make unplug, retry, and
rear-port recovery harder to reason about.

## Packet handling

The preferred path forwards complete Ethernet frames and does not interpret
Pro DJ Link. This carries all of the following unchanged:

- UDP discovery and status on ports 50000-50002;
- TCP 12523 and the runtime dbserver port;
- RPC port mapping;
- dynamic mountd UDP ports;
- NFSv2 on UDP 2049;
- later connections initiated in either direction.

NFS is the critical transport test. The direct capture includes replies with
8,292-byte UDP payloads fragmented across ordinary 1500-byte Ethernet frames.
Raw frame forwarding naturally preserves those fragments without NAPT state,
reassembly, or checksum changes. Validate that the S3 queues can sustain the
burst before attempting track loading.

If transparent forwarding fails because `rbp` demands the rear-USB protocol
personality, fall back to the server design:

1. First preserve raw forwarding and normalize only the captured rekordbox
   device-ID fields on UDP ports 50000-50002. LAN IP and MAC identities stay
   unchanged in this middle path.
2. If that is insufficient, translate UDP ports 50000-50002 at the application
   layer, including the captured IPv4, MAC, and device-ID fields.
3. Use a one-to-one routed mapping for every other protocol.
4. Preserve IP fragments consistently or reassemble and retransmit complete
   NFS datagrams.

The fallback translator must not synthesize claims, cache handshakes, or replay
stale packets. Its single-recordbox peer mapping is cleared on departure,
Wi-Fi loss, USB carrier loss, or an inactivity timeout.

## ESP32-S3 firmware structure

Start from Espressif's TinyUSB NCM device example, which already connects a
USB NCM host to an S3 Wi-Fi station. Split the firmware into these tasks:

| Task | Responsibility |
| --- | --- |
| USB network | TinyUSB NCM/ECM callbacks and RX/TX queues |
| Wi-Fi | station association, raw frame callbacks, reconnect events |
| Frame forwarder | raw Ethernet transfer between NCM and Wi-Fi driver hooks |
| Supervisor | link readiness, counters, watchdog, clean session reset |
| Diagnostics | bounded event log and counters over UART/control requests |

Keep the USB NCM link down until Wi-Fi association succeeds. On Wi-Fi loss,
drive NCM carrier down; after reassociation, drive it up again so the RX3
renews its address and restarts peer discovery instead of retaining stale
state. The application bootstrap waits for both carrier and a valid IPv4
address before reporting PC control ready.

Use bounded queues throughout. Classify Pro DJ Link control frames for priority
over NFS bulk traffic so library reads cannot delay discovery or status. A
Wi-Fi reconnect resets the USB-facing link long enough for `rbp` to discard the
old peer and restart discovery instead of preserving half-open state.

## Composite USB progression

Add USB functions in this order:

1. NCM only.
2. NCM plus read-only mass storage containing the RX3 module and configuration.
3. PCM over a prioritized IP stream on the NCM interface.

The final mass-storage function can make the S3 look like the toolkit drive
while the NCM function provides networking. A composite descriptor still needs
to be tested against the RX3 host stack; support in TinyUSB does not guarantee
that the product's mount manager accepts every composite layout.

The S3 has a tight endpoint budget. NCM consumes an interrupt IN endpoint and a
bulk endpoint in each direction; MSC consumes another bulk pair. Diagnostics
should therefore use IP on NCM or endpoint-zero control requests. The later PCM
path should also use the existing RX3 PCM tap over a prioritized IP stream
rather than adding UAC to the composite device. PCM16 stereo at 44.1 kHz
consumes 1.4112 Mbit/s before framing overhead, so NFS performance must be
measured first and bulk traffic must yield to audio and Link control frames.

## Milestones

### 1. Finish the server reference path

- Complete a clean Link Export session through the current server relay.
- Browse folders, list tracks, load both decks, and play.
- Record synchronized captures and relay counters for the successful session.
- Turn that capture into deterministic translator fixtures.

### 2. Bring up S3 USB networking

- Build exact-kernel `usbnet` and NCM/ECM modules.
- Flash an NCM-only S3 test image.
- Validate enumeration and sustained traffic on the RX3 without `rbp`.
- Select NCM or ECM from measured reliability and throughput.

### 3. Redirect `rbp`

- Swap the rear FEC and S3 interface names while `rbp` is stopped.
- Configure the S3 link before the application network state begins.
- Add the carrier-aware mount and activation shim.
- Verify native RX3 announcements and property requests on the S3 interface.

### 4. Port the control translator

- Forward frames unchanged using Espressif's one-host NCM design.
- Verify discovery, both rekordbox deck slots, and the source row.
- Introduce the server translator only if captures show a protocol-personality
  mismatch rather than an interface-selection or activation failure.

### 5. Port the data plane

- Pass dbserver port discovery through the raw frame path.
- Pass RPC and mountd discovery.
- Prove fragmented NFS reads with frame counters and packet captures.
- Browse and load tracks repeatedly across reconnects.

### 6. Package the device

- Add read-only MSC for the module/configuration.
- Automate module loading and interface setup.
- Add Wi-Fi provisioning and persistent credentials.
- Add an explicit management channel after the transport is reliable.

### 7. Add PCM

- Stream the existing PCM16 tap over the NCM interface.
- Measure audio continuity while loading tracks over NFS.
- Give control traffic strict priority and report audio drops explicitly.

## Success criteria

The Link Export milestone is complete when a cold RX3 boot with only the S3 in
USB-A produces all of the following without manual shell commands:

- the S3 joins the configured Wi-Fi network;
- rekordbox offers LINK and shows both RX3 decks;
- the RX3 SOURCE page shows the rekordbox computer;
- folders and tracks browse correctly;
- tracks load and play on both decks;
- unplug/replug recovers without rebooting the RX3 or rekordbox;
- the rear USB-B path remains recoverable when the S3 is absent;
- diagnostic counters show no control-plane loss or unresolved NFS fragments.

## Primary implementation risks

1. Interface failover requires restarting `rbp` because it caches the `eth0`
   ifindex and Pro DJ Link self MAC during initialization.
2. The RX3 3.0.101 NCM driver may not interoperate cleanly with current
   TinyUSB descriptors; ECM is the fallback.
3. Espressif's raw Wi-Fi frame APIs are private implementation interfaces, so
   the firmware must pin and test one ESP-IDF version.
4. A composite NCM and MSC descriptor may expose old-host compatibility issues.
5. USB Full Speed and Wi-Fi share enough S3 CPU and memory pressure that bulk
   NFS and PCM need explicit queueing and priority.

References:

- [Espressif ESP32-S3 USB device stack](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)
- [Espressif TinyUSB NCM example](https://github.com/espressif/esp-idf/tree/master/examples/peripherals/usb/device/tusb_ncm)
- [Espressif esp_tinyusb component](https://github.com/espressif/esp-usb/tree/master/device/esp_tinyusb)
- [Espressif USB Full Speed throughput FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/usb.html#why-does-esp32-s2-esp32-s3-not-reach-the-maximum-usb-full-speed-12-mbps)
