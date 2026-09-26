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
contains `udhcpc`, and `rbp` starts it with a separately embedded interface
argument. The autoexec bootstrap patches both the application interface and
the DHCP command from `eth0` to `usb0`, so the stock client obtains and
maintains the LAN lease after NCM carrier rises. A failed lease retains the
stock `169.254/16` fallback for diagnosis but does not represent a usable LAN
connection.

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

- preserve IP and MAC fields;
- preserve UDP device identity `0x29`;
- connect directly to the rekordbox dbserver;
- forward RPC, mountd, and NFS traffic unchanged.

This is a testable expectation, not an assumption that USB attachment is
irrelevant. Capture both sides during the first Link attempt. If they show a
mixed identity, normalize the relevant UDP identity and the first dbserver
response together. Translating only one side recreates the proven failure.

The existing routed translator remains the fallback if `rbp` accepts only the
rear-USB rekordbox personality. In that design the S3 terminates each side,
translates the captured IP, MAC, and device-ID fields on UDP 50000-50002, and
routes the remaining unicast traffic.

### RX3-side personality fallback

The routed server's translation requirements do not all carry over to the
transparent S3 topology. Its IP and MAC substitutions reconcile two separate
subnets and two virtual endpoints. The S3 instead gives the RX3 one LAN address
and forwards its Ethernet frames with the shared Wi-Fi/NCM MAC. Dynamic
dbserver discovery, RPC, mountd, NFSv2, and fragmented replies can therefore
remain byte-transparent.

The first dbserver response is the one content rewrite with an isolated live
result. The routed session registered rekordbox as USB device `0x11`, then
received a first dbserver response naming LAN device `0x29`; the RX3 closed the
connection. Replacing that final typed `UInt32` with `0x11` allowed menu and
track requests to continue. This proves that mixed identities fail. It does
not prove that a consistent `0x29` session fails.

Static analysis identifies the comparison. `DBComm_OnMessage` reads the
discovered peer ID from `message + 0x0b` at `0x00149724` and passes it to
`DtStrm_Connect` at `0x00149774`. The handler explicitly admits IDs in the LAN
PC range at `0x00149738`-`0x00149740` and `0x001497c4`-`0x001497cc`.
`DtStrm_Connect` reads the first dbserver response identity from
`response + 0x18` and compares it with the discovered ID at `0x001f5354` and
`0x001f5394`. The successful path stores that same expected ID at
`stream + 4`; there is no hardcoded `0x11` check. A consistent `0x29` session
therefore follows the stock success path.

Test the unmodified, consistent LAN personality first. Capture the USB NCM
interface and Wi-Fi LAN together and verify that the following values remain
`0x29` on both sides:

- the 54-byte type `0x06` announcement at byte 36;
- source identity fields in the type `0x11`, `0x29`, and `0x47` responses;
- the final typed `UInt32` in the first dbserver response.

If that session reaches peer registration but the RX3 rejects browsing, keep
the S3 as a raw bridge and patch the comparison inside `rbp` with a
firmware-guarded preload constructor. These functions are absent from
`.dynsym`, and their internal calls use direct branches, so ordinary symbol
interposition cannot hook them. Firmware 1.19 contains `cmp sl,r2` at virtual
address `0x001f5394`, file offset `0x001ed394`, encoded as
`02 00 5a e1`. Replacing it with `cmp sl,sl` (`0a 00 5a e1`) makes the existing
conditional store record the already-learned expected ID in `stream + 4` and
takes the existing success branch. The constructor must verify the exact
firmware bytes, temporarily make the non-PIE text page writable, clear the
instruction cache, and restore execute permissions.

That one-instruction fallback accepts every server-ID mismatch in this connect
handler. Enable it only when a capture proves that the transparent path still
produces a mixed identity. A larger code-cave patch can instead permit only the
known `0x11`/`0x29` pairing. Either patch should leave IP addresses, MAC
addresses, port discovery, RPC, mountd, NFS, and all later dbserver messages
unchanged.

Only move this translation to the S3 if an RX3-side hook cannot be made stable.
That fallback would require the S3 to parse UDP 50000-50002 and the initial
dbserver stream while continuing to forward the large fragmented NFS data path
without application-layer processing.

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
4. Keep NCM carrier down until Wi-Fi association completes, then run a manual
   `udhcpc` probe on RX3 `usb0`.
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
hardware. Static analysis confirms that `rbp` hardcodes a shared `eth0` string
at virtual address `0x003fe678` across its network stack. The direct users
include:

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

The stock DHCP command contains a second literal at file offset `0x004d955a`:
`udhcpc -i eth0 -T 2 -t 3 -n -q`. Its three two-second attempts explain the
quick AutoIP fallback seen during live testing. The autoexec runtime verifies
the complete 1.19 executable and guarded bytes at `0x003f6678`, `0x004d9558`,
and `0x004d955c`. The latter two aligned words span the DHCP literal beginning
at `0x004d955a`. Together they replace both instances of `eth0` with `usb0`
before restarting `rbp`. Rear USB-B keeps its kernel name `eth0`, while NCM
keeps `usb0`.

The prepare hook first loads the host drivers, matches the S3 by USB VID, PID,
product string and topology, verifies that its interface is `usb0`, and waits
for carrier. Failure aborts the runtime before the guarded word is written, so
rear USB-B remains the stock recovery path. Hot-switching underneath a running
`rbp` remains unsafe because its listener retains the original ifindex and Pro
DJ Link retains the original self MAC.

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
2. NCM plus writable mass storage containing the RX3 module and configuration.
3. PCM over a prioritized IP stream on the NCM interface.

The final mass-storage function can make the S3 look like the toolkit drive
while the NCM function provides networking. A composite descriptor still needs
to be tested against the RX3 host stack; support in TinyUSB does not guarantee
that the product's mount manager accepts every composite layout.

Firmware 1.19 carries a `CONFIG_PDJ` change in `usb_set_configuration()` that
breaks out of interface registration immediately after adding a mass-storage
interface. The S3 descriptor must order its functions as NCM control, NCM data,
then MSC. Espressif's default MSC-first descriptor makes the storage volume
work while leaving no NCM interface in the RX3 sysfs tree.

The S3 has a tight endpoint budget. NCM consumes an interrupt IN endpoint and a
bulk endpoint in each direction; MSC consumes another bulk pair. Diagnostics
should therefore use IP on NCM or endpoint-zero control requests. The later PCM
path should also use the existing RX3 PCM tap over a prioritized IP stream
rather than adding UAC to the composite device. PCM16 stereo at 44.1 kHz
consumes 1.4112 Mbit/s before framing overhead, so NFS performance must be
measured first and bulk traffic must yield to audio and Link control frames.

## Live NCM and DHCP validation

The composite bridge completed its first full RX3-to-LAN test on 2026-09-25.
Firmware 1.19 loaded the packaged `usbnet.ko` and `cdc_ncm.ko`, detected the S3
at USB topology `2-1.2`, and created `usb0` with the S3 station MAC
`68:ee:8f:49:64:ec`. The management module assigned `172.31.254.2/30`, and a
LAN host at `172.31.254.1/30` opened the RX3 BusyBox root shell through Wi-Fi,
the S3 bridge, and USB NCM.

A non-applying `udhcpc` probe on `usb0` then completed the full
discover/offer/request/ACK exchange and received `10.0.0.131/24` from the LAN
DHCP server. Applying that lease as the secondary alias `usb0:lan` preserved the
management address and rear USB `eth0`. The server opened the same RX3 shell at
`10.0.0.131`, and the RX3 reached gateway `10.0.0.1` with three successful ICMP
replies. This proves bidirectional unicast, ARP, DHCP broadcast, and DHCP reply
traffic through the transparent bridge.

The lease test deliberately omitted a default route. Link Export peers are on
the directly connected LAN, and changing the default route is unnecessary for
discovery, dbserver, RPC, mountd, or NFS. The production path lets the stock
`rbp` DHCP client own the primary `usb0` address, then adds the private `/30`
recovery address as `usb0:mgmt`.

A 2026-09-26 retest without an external S3 antenna isolated the remaining
failure. The patched `rbp` and an interactive invocation both ran the exact
stock command `udhcpc -i usb0 -T 2 -t 3 -n -q`. Each produced three Discovers;
the RX3 `usb0` counter and S3 `usb_rx` counter increased by three, and
`esp_wifi_internal_tx()` returned `ESP_OK` without increasing `usb_drop`.
Simultaneous filtered and unfiltered captures on `lan0` saw none of those DHCP
frames. Thus the current failure is after NCM receipt and Wi-Fi driver queue
acceptance. It is not an RX3 DHCP-client or interface-selection failure.

The S3 forwarding path matches ESP-IDF 6.1's `tusb_ncm` bridge example. The same
firmware path previously completed DORA and reached the gateway, while the
antenna-less link later showed severe packet loss. The next firmware exposes
physical TX completion counters (`tx_ok`, `tx_fail`) and associated `rssi` in
`RX3STAT1`. Repeat the exact stock DHCP command after attaching the antenna. A
rising `tx_fail` counter confirms RF delivery failure; `tx_ok` with no LAN copy
points instead to the AP or capture path.

The application-facing kernel names remain unchanged: rear USB is `eth0` and
the S3 is `usb0`. Three aligned guarded words select `usb0` for the application
network stack and its independently embedded DHCP command without renaming
either interface. The first live autoexec patched only the shared network
string, so DHCP still ran on rear `eth0` while AutoIP fallback was configured
on `usb0`.

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

- Guard and patch the shared `rbp` interface and DHCP literals from `eth0` to
  `usb0`.
- Raise the S3 link before the application network state begins.
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

- Add writable MSC for the module/configuration. Firmware 1.19's VFAT udev rule
  mounts with `-o rw` before calling `decrypt_autoexec.sh`, so USB-level write
  protection prevents the bootstrap from running.
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
