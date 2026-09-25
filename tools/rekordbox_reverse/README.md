# Rekordbox Link Export static analysis

This directory records static analysis of the macOS Rekordbox Link Export
implementation. The application was extracted and inspected without executing
any Rekordbox binary. All package extraction and reverse-engineering tools ran
inside the rootless `localhost/rx3-reverse:latest` Podman image with networking
disabled.

## Input and provenance

The analyzed installer is the official Rekordbox 7.2.19 macOS package, released
September 24, 2026:

- URL: <https://cdn.rekordbox.com/files/20260914095016/Install_rekordbox_7_2_19.pkg_.zip>
- ZIP size: 936,293,132 bytes
- ZIP SHA-256: `17d9bf51c75df6f06be73f039c7acd95bcbb2700c10ff39cf360034fec8a2e03`
- Package identifier: `com.pioneer.rekordbox.7.2.19.0342`
- Package version: `7.2.19.0342`
- App bundle identifier: `com.pioneerdj.rekordboxdj`

The XAR TOC contains RSA and CMS signatures and a certificate chain whose leaf
is `Developer ID Installer: AlphaTheta Corporation (6BRHGXQ6VU)`. The package
TOC creation time is September 2, 2026 at 05:33:13 UTC.

The main executable is a universal x86-64/arm64 Mach-O file:

- Size: 301,587,792 bytes
- SHA-256: `07dcecbaf304f777e435539c83c7dcc02e41de1eaf2e0a75e8a9852e4411a244`
- x86-64 slice size: 166,373,984 bytes

Run `extract.sh` with the downloaded ZIP and a new output directory to
reproduce extraction. It deliberately performs no download and gives the
container no network access.

## How Rekordbox selects the Link interface

The core Link Export path does not contain a USB VID/PID allowlist, an RX3
model check, an `enX` interface-name check, or a hard-coded `169.254/16` check.
Instead, it derives the interface from the source address of received Link UDP
traffic and then classifies the selected interface as Wi-Fi or non-Wi-Fi.

The x86-64 addresses below are unslid virtual addresses from the 7.2.19 main
executable.

| Address | Function or operation | Evidence |
| --- | --- | --- |
| `0x100c08070` | `PSvLinkUDP::run()` | Receive loop. |
| `0x100c08159` | `recvfrom()` | Receives up to 1,472 bytes and captures the sender socket address. |
| `0x100c0818a`–`0x100c08190` | Store sender IPv4 | Copies the latest successful sender IPv4 into `PSvLinkUDP + 0x1e1`. |
| `0x100c07d00` | `PSvLinkUDP::getConnectedIPAddress()` | Returns the address at `PSvLinkUDP + 0x1e1`. |
| `0x100cddb70` | `PSvLinkSysMgrNetworkAccess::setMyIFAddrs()` | Gets the connected address, enumerates `getifaddrs()` entries, and selects an AF_INET interface on the same subnet. |
| `0x100cddd1a`–`0x100cddd34` | Subnet comparison | Compares the local and remote addresses under the candidate interface's netmask. |
| `0x100cdde68` | `IORegistryEntryCreateCFProperty()` | Reads `IOMACAddress` from the IOKit service matching the selected BSD interface name. |
| `0x100cde230` | `PSvLinkSysMgrNetworkAccess::getLinkIF()` | Matches that MAC against SystemConfiguration services and tests for `kSCNetworkInterfaceTypeIEEE80211`. |
| `0x10132bf80` | `SysMgrMainComponent::linkUpFunc()` | Orchestrates interface selection and Link discovery. |
| `0x10132c022` | Call to `setMyIFAddrs()` | Link-up aborts at `0x10132c226` when interface selection fails. |
| `0x10132c12e` | Call to `getLinkIF()` | Stores the Wi-Fi/non-Wi-Fi result at `SysMgrMainComponent + 0x1b1`. |

`getLinkIF()` returns zero for an IEEE 802.11 service and one for other
interfaces. USB Ethernet and ordinary wired Ethernet therefore take the same
branch. This is high-confidence static evidence that the core Link path does
not require the RX3 USB NIC specifically.

The selected sender IP matters. Every accepted receive overwrites the stored
address. When Link Up runs, the most recently stored sender determines which
local subnet and interface Rekordbox chooses. Relaying a packet with an
unexpected source address, or racing traffic from another interface, can make
`setMyIFAddrs()` select the wrong interface or fail entirely.

## Wi-Fi and non-Wi-Fi Link personalities

The interface classification changes fields and timing in the discovery state
machine:

| Path | Code | Selected Link identity byte | Timing/state fields |
| --- | --- | --- | --- |
| Non-Wi-Fi | `0x10132c1da`–`0x10132c214` | `0x17` when the prior mode is `0x17`, otherwise `0x11` | 100 ms and state `1` |
| Wi-Fi | `0x10132c303`–`0x10132c3ca` | Reads SSID/BSSID, then uses `0x17` when already in that mode, otherwise `0x29` | 200 ms and state `20` |

Both paths converge at `0x10132c479`, call
`SysMgrMainComponent::frameSendDiscoveryRequest()` at `0x10132c483`, and start
their timers. `frameSendDiscoveryRequest()` begins at `0x10132c610`, constructs
a 44-byte discovery frame containing the selected local MAC, and broadcasts it.

The selected byte is later logged and validated as an `id`; the exact field
name is inferred because complete type information is unavailable. This
distinction is the strongest explanation found for why a byte-forwarding relay
behaves differently from a direct RX3 USB connection. Direct USB chooses the
non-Wi-Fi path. Traffic arriving through the MacBook's Wi-Fi interface causes
Rekordbox to choose the Wi-Fi path and use different Link identity, timing,
SSID, and BSSID data. A relay that only rewrites IP and MAC addresses therefore
does not reproduce the direct USB state machine.

This is a protocol personality difference rather than a USB-NIC authorization
check. A synchronized direct-USB and relayed capture should locate the Link
identity byte containing `0x11`, `0x17`, or `0x29`, compare any SSID/BSSID
extension and request cadence, and record the order in which the first control
packet reaches Rekordbox.

## Other Link-specific observations

- `PSvLinkUDP::startRecv(int)` at `0x100c07c20` binds an IPv4 UDP socket on
  `0.0.0.0:<port>` and starts its receive thread.
- The main binary contains explicit diagnostics for ports 2049, 50000, 50001,
  50002, and 50111.
- The only model-specific rejection in the analyzed `linkUpFunc()` block is an
  old-version compatibility condition for exact model strings `CDJ-2000` and
  `CDJ-900` at `0x10132c3d7`–`0x10132c5d7`. It is unrelated to RX3 USB
  interface selection.
- The `XDJ-RX3` string is present in the main executable, but no RX3-specific
  check was found in the interface-selection or Link Up path.

## NFS authorization evidence

Rekordbox 7.2.19 maintains an explicit, dynamic Link-member allowlist for its
embedded NFS service:

- `NFSdMainComponent::addNFSPermission()` begins at `0x101a2cf30`.
- It parses a Link member ID and IPv4 address, loads
  `libFilSiNE_Mac_DyLib.dylib`, resolves `AddLinkMemberToList`, and passes the
  member address and ID to it.
- Deletion resolves `DeleteLinkMemberFromList` at `0x101a2d080`.
- Symbols also expose `requestSetPermittedFileExtensionList()`.

This proves that 7.2.19 has per-member/IP NFS authorization machinery. It does
not establish what changed in the 7.2.17 "Enhanced LINK EXPORT security"
release. Answering that requires an official 7.2.16 installer and a binary or
configuration diff against 7.2.17.

## Generated analysis files

The local `analysis/`, `extracted/`, and `input/` directories are ignored
because they contain copyrighted binaries and large generated output. Useful
generated evidence includes:

- `analysis/psvlinkudp-disasm.txt`
- `analysis/link-up-func-disasm.txt`
- `analysis/nfs-permission-disasm.txt`
- `analysis/target-symbols.txt`

`find_x86_calls.py` scans the x86-64 slice for direct `call rel32`
instructions targeting one or more virtual addresses and attributes each call
to the nearest known function symbol.

## September 25 synchronized capture

Four simultaneous captures cover the server's physical LAN, its `rx3lan`
macvlan, the RX3 USB Ethernet interface, and the MacBook. The compact inputs
are:

- `captures/macbook/rekordbox-20260925-124729-control.pcap`
- `captures/link-export-20260925T164951Z/server-lan0-control.pcap`
- `captures/link-export-20260925T164951Z/server-rx3lan-control.pcap`
- `captures/link-export-20260925T164951Z/server-usb-control.pcap`

Run the comparison inside the offline analysis container:

```sh
podman run --rm --network=none \
  -v "$PWD/captures:/captures:ro" \
  -v "$PWD/tools/rekordbox_reverse:/work:ro" \
  localhost/rx3-reverse:latest \
  sh -c 'cd /work && python3 compare_link_handshake.py \
    /captures/link-export-20260925T164951Z/server-rx3lan-control.pcap \
    /captures/link-export-20260925T164951Z/server-usb-control.pcap'
```

`pcap_link.py` is a dependency-free classic-PCAP/Ethernet/IPv4 parser used by
the comparator. This keeps packet inspection inside the offline Podman
environment even though the image has no `tcpdump`, `tshark`, Scapy, or dpkt.

### Identity translation matches the static analysis

The first fresh Rekordbox discovery request is a 44-byte type `0x00` packet at
16:53:15.513 UTC. On the LAN it contains the MacBook Wi-Fi MAC
`1c:57:dc:39:00:bb` at offset 38. On USB the relay replaces it with
`c8:3d:fc:16:af:9a` at the same offset.

The stable type `0x06` announcement at 16:54:26.017 UTC contains:

| View | Link ID at offset 36 | Advertised MAC | Advertised IPv4 |
| --- | --- | --- | --- |
| LAN | `0x29` | `1c:57:dc:39:00:bb` | `10.0.0.119` |
| USB | `0x11` | `c8:3d:fc:16:af:9a` | `169.254.100.1` |

This is the exact `0x29` Wi-Fi to `0x11` non-Wi-Fi transformation predicted
by `SysMgrMainComponent::linkUpFunc()`. The corresponding type `0x00`, `0x02`,
and `0x06` packets have equal lengths on LAN and USB. No separate SSID/BSSID
extension appears in these captured packet classes; the static SSID/BSSID read
is part of Rekordbox's internal Wi-Fi state even when it does not add a visible
extension here.

### The first missing packet is the type 0x11 source description

The session reaches RX3 unicast negotiation and then fails in one direction:

1. At 16:54:26.024691 UTC, the RX3 sends a 36-byte type `0x10` request from
   `169.254.100.2:39793` to `169.254.100.1:50002`.
2. The translated request reaches the LAN as
   `10.0.0.253:39793 -> 10.0.0.119:50002`.
3. At 16:54:26.147306 UTC, the MacBook replies from UDP/50002 with a 296-byte
   type `0x11` packet addressed to `10.0.0.253:50002`.
4. That response contains the UTF-16BE name `macbook-air` and Link identity
   `0x29` at offsets 33 and 36.
5. The response is visible on both server LAN captures and never appears on
   the USB capture.

The sequence repeats every 5.02 seconds. The paired captures contain 13 RX3
type `0x10` requests and 13 LAN type `0x11` replies, while USB contains zero
type `0x11` replies. This is the first packet loss after Link activation and
directly prevents the RX3 from receiving the name and properties needed to
create the Rekordbox source row.

The RX3 also sends one type `0x30` request. It reaches the MacBook, but no type
`0x31` response exists in any capture. No type `0x46`/`0x47` exchange occurs.
Those later exchanges cannot explain the earlier loss of the valid type
`0x11` replies.

### Reverse-path filtering explains the socket-level loss

The missing type `0x11` frame has valid IPv4 and UDP checksums in the MacBook,
`lan0`, and `rx3lan` captures. Its Ethernet destination is exactly the
`rx3lan` MAC, so it reaches the macvlan at layer 2.

The host has both of these values set to strict mode:

```text
/proc/sys/net/ipv4/conf/all/rp_filter=1
/proc/sys/net/ipv4/conf/default/rp_filter=1
```

`setup-nat.sh` currently writes zero only to the individual `lan0`, `rx3lan`,
and USB interface settings. Linux performs source validation using the maximum
of `conf/all/rp_filter` and `conf/<interface>/rp_filter`, as documented in the
[kernel IP sysctl reference](https://www.kernel.org/doc/html/v6.17/networking/ip-sysctl.html#rp-filter-boolean).
The global value therefore leaves strict filtering enabled.

The type `0x11` frame arrives on `rx3lan` from `10.0.0.119`, while the reverse
route to that MacBook address uses the physical LAN interface. Strict reverse
path validation discards it between the AF_PACKET capture point and UDP socket
delivery. That precisely explains the evidence: packet capture sees the frame
on `rx3lan`, but the relay's UDP listener cannot translate and transmit it on
USB. Broadcast discovery arrives through the physical LAN path and continues
to work.

The setup needs to disable `conf/all/rp_filter` for this asymmetric macvlan
topology, restoring its previous value during teardown. Loose mode `2` is
another possible policy, but should be verified with a live type `0x10`/`0x11`
exchange.

### Status cadence is not relay amplification

The 619 type `0x0a` packets are the RX3's two deck-status streams. Deck IDs
`0x0b` and `0x0c` each send at a median interval of 213.9 ms, staggered by
roughly 100 ms. Packet counts and payloads match one-for-one between USB and
`rx3lan`. The high count is normal per-deck status cadence rather than a relay
feedback loop.

### Rekordbox receive gates for types 0x30 and 0x46

Static analysis of Rekordbox 7.2.19's x86-64 slice identifies different gates
for these requests. The type `0x10`/`0x11` name exchange does not exercise
either gate, so a successful `0x11` reply only proves basic UDP/50002 delivery.

`PSvLinkNormalInterval::messageReceived()` at `0x100b76b20` dispatches type
`0x30` through jump-table entry 43 to `0x100b77189`. The handler first requires
the byte at `PSvLinkNormalInterval + 0x1185d` to be nonzero
(`0x100b77190`-`0x100b77197`).
`PSvLinkNormalInterval::enableDeviceSearch(bool)` at `0x100b768d0` is the
setter for this byte. The handler then walks a linked list rooted at
`PSvLinkNormalInterval + 0x11840` and suppresses a source IPv4 address already
present in the list (`0x100b7719d`-`0x100b771c5`). For a new source it stores
the address, parses the packet with `PSvLinkDeviceSearchResInfo::setData()` at
`0x100b771f2`, and appends message type `0x34` at `0x100b772b5`. This receive
path does not read the player-status capability byte described below, nor does
it compare Wi-Fi identity `0x29` with wired identity `0x11`. The static trace
establishes the receive and queueing gates; it does not yet identify the later
callback that emits type `0x31`.

Type `0x46` matches in
`PSvLinkDevSettingReadReqInfo::matchMessage()` at `0x100b78c50` and is queued
without an interface check. Its reply path,
`PSvLinkNetworkAccess::unicastDevsettingReadRes()` at `0x101db6820`, calls
`PSvLinkNormalInterval::isSupportDeviceSetting()` at `0x101db6840`. A false
result branches to the no-send return at `0x101db6935`. A true result constructs
`PSvLinkDevSettingReadResInfo`, whose constructor writes type `0x47` at
`0x101db7af4`, and unicasts it at `0x101db6905`.

The capability predicate at `0x100b765d0` selects the canonical peer slot by
device ID. IDs 1 through 8 use
`this + 0x2b3 + (id - 1) * 0xb8`; IDs 9 through 12 use
`this + 0xe33 + (id - 9) * 0xb8`. At `0x100b76604` it reads that byte and
returns bit 7.

That byte comes directly from status type `0x0a`, rather than from a type
`0x31` response. The `0x0a` dispatch selects a slot using on-wire byte `0x21`
and calls `PSvLinkPlayerLinkInfo::setData()` at `0x100b76fc3` for IDs 1 through
8 or `0x100b77378` for IDs 9 through 12. `setData()` copies on-wire byte
`0x8b` verbatim into parsed-object byte `0x8b` at
`0x101056d07`-`0x101056d0d`. The later canonical-slot copy in
`messageReceived()` maps parsed byte `0x8b` to the exact byte read by
`isSupportDeviceSetting()`.

In the relayed live capture, status packets for both RX3 deck IDs have
byte `0x8b = 0x9e`, so bit 7 is set after the first status packet for each ID.
The first type `0x30` arrives seven microseconds before the first type `0x0a`.
The known-good direct session also ignores its initial `0x30` and `0x46`, then
answers retries after status traffic has begun. The relayed RX3 sends only one
`0x30`, leaving no post-status retry to answer. This timing fully explains an
initial `0x46` suppression through the proven capability gate. For `0x30`, the
proven static gates are the device-search enable byte and duplicate source-IP
list, not the status capability bit.

The `0x0a` capability setter selects its slot by packet byte `0x21` and does
not compare the UDP source address. Therefore the relayed mismatch between
source `10.0.0.253` and an embedded `169.254.175.153` address in type `0x06`
does not prevent the capability bit itself from being stored. The mismatch can
still affect member association elsewhere, but this trace does not establish
that as a gate for either response.
