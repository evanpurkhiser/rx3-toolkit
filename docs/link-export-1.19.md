<!-- SPDX-License-Identifier: MPL-2.0 -->
# Firmware 1.19 Link Export

This document describes direct and relayed Link Export sessions between
rekordbox on macOS and an XDJ-RX3 running firmware 1.19. The server relay was
verified through live folder and track-list browsing on September 25, 2026. It
also records why earlier prototypes stopped at peer registration, SOURCE
rendering, or the first dbserver response.

## Capture evidence

The capture module recorded the RX3's complete `eth0` interface and three
`rbp` USB callbacks. The recovered files were:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `eth0.pcap` | 11,547,061 bytes | `2019f2f88cd8f45d65078c5d7ac20581722bf91cb97de54842365fe4a5df8802` |
| `rekordbox-usb.jsonl` | 65,866 bytes | `d97e26d5e556f9846bc67d2d84f1a1de1fc4e4480f93ea3961fc28935645266d` |

The PCAP contains 10,834 packets over 77.929 seconds. Every recorded packet has
equal captured and original lengths. The final boundary leaves one incomplete
IPv4 fragment set, so packet loss outside the capture interval cannot be ruled
out.

The direct peers used these identities:

| Peer | MAC | IPv4 |
| --- | --- | --- |
| RX3 | `c8:3d:fc:16:af:99` | `169.254.175.153` |
| Mac / rekordbox | `c8:3d:fc:16:af:9a` | `169.254.171.125` |

## Target network model

The RX3 presents one Pro DJ Link network appliance, not two independent CDJs.
It has one MAC address, one IPv4 address, and one discovery identity. The two
decks are logical players inside that appliance: captured status packets use
player IDs `0x0b` and `0x0c`, while both packets come from the same network
endpoint. Rekordbox can therefore render and address two decks without two IP
addresses.

The LAN relay gives each peer the native-looking endpoint it expects:

| View | RX3 identity | rekordbox identity |
| --- | --- | --- |
| rekordbox LAN | one virtual LAN IPv4 address, the RX3 MAC, stock player IDs `0x0b`/`0x0c` | the Mac's LAN address, MAC, and LAN-selected device ID |
| RX3 USB link | the RX3's link-local address and stock player IDs | one virtual link-local address, the USB-host MAC, and device ID `0x11` |

This is address and identity translation around the stock protocol. The relay
does not model two CDJs, manufacture deck state, or implement the rekordbox
library service. UDP ports 50000-50002 carry the translated Pro DJ Link control
plane. A focused TCP broker normalizes the initial dbserver identity. Kernel
NAT carries RPC, mountd, NFS, and dynamically selected UDP ports between the
same two peers.

The direct capture is sufficient to specify the USB side and the complete data
plane. It contains discovery, USB activation, the device-property exchange,
dbserver negotiation, RPC port mapping, mountd, NFSv2, fragmentation, and
sustained library reads. It does not by itself prove how rekordbox expects every
control packet to be delivered on a Wi-Fi LAN. A paired LAN reference capture,
or a simultaneous capture of both relay interfaces during a successful
session, is still needed to validate these medium-specific details:

- which post-discovery control packets remain unicast and which use broadcast;
- every device-ID field that changes between rekordbox's LAN and USB
  personalities;
- whether any packet contains an endpoint identity outside the known IP, MAC,
  and device-ID fields;
- the exact timing and lifetime rules around the `0x30`/`0x31`, `0x10`/`0x11`,
  and `0x46`/`0x47` exchanges on a LAN.

The implementation should remain a packet translator while those questions
are measured. Handshake caching, replay, and synthetic protocol state can hide
an incorrect translation and are outside the normal relay path.

## External LAN references

Deep Symmetry's Dysentery repository includes a July 2026 hardware capture
corpus recorded with two CDJ-2000NXS players running firmware 1.44. The most
useful scenarios for this relay are:

| Capture | Relevant behavior |
| --- | --- |
| `S01-cdj-startup.pcapng` | Address selection and device-number claiming |
| `S02-keepalive.pcapng` | Native LAN broadcast announcements |
| `S04-cdj-status.pcapng` | Unicast player status on UDP port 50002 |
| `S05-connection.pcapng` | Full peer connection through port 12523 and dbserver port 1051 |
| `S13-media-query.pcapng` | Media query and response |
| `S15a-dbserver-request.pcapng` | dbserver request and response |
| `S16a-rpc.pcapng` | RPC and NFS traffic |
| `S20-link-play.pcapng` | Browse, load, and play from linked media |

These captures establish an important delivery rule for native Ethernet:
discovery and keepalive traffic is broadcast, while player status and dbserver
traffic is unicast. The capture notes also warn that a bridge capture can miss
unicast packets exchanged directly between the players. The relay should
therefore preserve unicast delivery instead of converting the entire control
plane to broadcast.

The older Dysentery `LinkInfo`, `Sync-Master`, `powerup`, and `to-virtual`
captures contain CDJ, DJM, and virtual-CDJ traffic. None of the committed
captures contains a rekordbox Export peer or the RX3-specific `0x30`/`0x31`,
`0x10`/`0x11`, and `0x46`/`0x47` exchanges seen in the direct USB capture.

Dysentery issue 1 links two external CloudShark captures from an XDJ-RX: a
normal startup and a startup followed by a Dysentery connection. The packet
analysis in the issue shows that the XDJ-RX claims one network identity with
device type `0x07` and initially selects device number `0x0b`. This supports
the single-appliance model used for the RX3, but the captures do not include a
rekordbox Export session.

Beat Link Trigger issue 43 records that complete rekordbox Export Wireshark
captures were used to diagnose a dbserver framing problem. Those files were
exchanged privately and are not attached to the issue or committed in the
Deep Symmetry repositories. The resulting finding remains useful: some
rekordbox versions required each dbserver message to be sent as one contiguous
TCP write even though TCP itself is a byte stream.

The public captures are enough to validate generic LAN claiming, delivery,
dbserver, RPC, NFS, and browse behavior without another deck. They cannot
specify the medium-specific rekordbox identity translation or the RX3 session
gates listed above. The direct RX3 capture remains the authoritative USB-side
reference for those fields.

References:

- [Dysentery hardware capture corpus](https://github.com/Deep-Symmetry/dysentery/tree/main/doc/assets/captures)
- [Dysentery packet analysis](https://djl-analysis.deepsymmetry.org/djl-analysis/packets.html)
- [XDJ-RX startup captures and analysis](https://github.com/Deep-Symmetry/dysentery/issues/1)
- [Rekordbox Export dbserver capture discussion](https://github.com/Deep-Symmetry/beat-link-trigger/issues/43)

## Paired LAN capture procedure

Use one continuous capture for the complete experiment. Start every capture
before opening rekordbox so the files include address selection, discovery,
device-number claiming, LINK activation, source selection, and the library
data plane.

The Mac streams its active-interface capture directly to the server. Run this
in a Mac terminal and leave it running:

```bash
iface=$(route -n get default | awk '/interface:/{print $2}')
stamp=$(date -u +%Y%m%dT%H%M%SZ)
sudo -v &&
sudo /usr/sbin/tcpdump -i "$iface" -n -U -s 0 -w - |
  ssh evan@server \
    "mkdir -p /home/evan/workspace/rx3-research/toolkit-source/captures/macbook && cat > /home/evan/workspace/rx3-research/toolkit-source/captures/macbook/rekordbox-$stamp.pcap"
```

On the server, install `tcpdump` if necessary, then arm the three server-side
interfaces with:

```bash
ssh-agent-ctx "Capture the RX3 Link Export session" -- \
  sudo tools/rx3_link_export/capture-session.sh
```

The script captures `lan0`, the `rx3lan` relay endpoint, and the RX3 USB NIC.
It finds the USB NIC by its stable MAC address instead of its variable Linux
interface name. Keep both terminals running through this sequence:

1. Start rekordbox and wait for its initial announcements.
2. Enable LINK and wait for both RX3 deck slots to appear in rekordbox.
3. Open the RX3 SOURCE page and wait for the Mac source row.
4. Select the Mac source, browse at least one folder, and open one track list.
5. Load a track to each deck and start playback once.
6. Leave the session idle for ten seconds, then stop the server capture and the
   Mac capture with Ctrl-C.

Do not restart the relay or either endpoint after capture begins. Record any UI
result and the approximate step at which it appeared. Packet contents provide
the exact cross-host alignment even if the two system clocks differ slightly.

The toolkit's USB Link root-shell module adds `169.254.100.2/16` to RX3 `eth0`
on every load. The relay uses that stable address by default. Its LAN-facing
RX3 identity is also fixed at `10.0.0.253`, and the server side of the USB link
is fixed at `169.254.100.1`. The rekordbox computer's LAN DHCP lease is the only
address that still needs a reservation or discovery during relay startup.

## USB activation

The USB callback log has 400 contiguous records with no reported drops: one
start record and 399 host-to-RX3 MIDI messages. It contains no HID traffic in
either direction. Link Export browsing therefore does not depend on the HID
controller channel.

After one empty SysEx and one MIDI-CI discovery message, rekordbox sends this
Pioneer SysEx 397 times:

```text
f0 00 40 05 00 00 03 0d 00 50 01 f7
```

The messages span 79.918 seconds with a mean interval of 201.814 ms. Firmware
dispatches command `0x50` to `PcControlCert::rcvActivateCmd`. That method sets
the active state, calls its state callback, and restarts a one-second expiry
timer. The repeated SysEx is the entire certification lease; the adjacent
certificate command handlers are stubs in this build.

`PcController::handleUsbMountMessage` handles the stock USB mount event. Its
mounted case sets `PcController+0x72`, initializes mixer key data, and starts
MIDI reception. `NetworkMonitor` samples that byte on its one-second timer and
advances the normal application state through a callback. Writing `+0x72`
directly and immediately forcing `SystemManager` state 5 skips those stock side
effects and ordering.

## Network sequence

Both endpoints first choose IPv4 link-local addresses. RX3 Pro DJ Link traffic
begins 8.676 seconds into the capture. Its discovery and device-ID claim
sequence uses packet types `0x0a`, `0x00`, `0x02`, and `0x04`, followed by a
type `0x06` announcement about every two seconds.

rekordbox appears at 18.901 seconds. It sends type `0x29` at about 10 Hz, runs
the type `0x00` and `0x02` claim sequence, selects device ID `0x11`, and begins
its own two-second type `0x06` announcement. A 296-byte type `0x11` packet
carries the UTF-16 computer name `macbook-air`. A later 192-byte type `0x06`
contains the UTF-16 product name `rekordbox`.

The captured Pro DJ Link packets embed addresses at these known offsets:

| Sender | Type | Address offset |
| --- | --- | ---: |
| rekordbox | `0x02` | 36 |
| rekordbox | `0x06` | 44 |
| RX3 | `0x02` | 36 |
| RX3 | `0x05` | 36 |
| RX3 | `0x06` | 44 |

Once discovery completes, the RX3 queries rekordbox's TCP port 12523 for
`RemoteDBServer`. The first response is `0xffff`; the later response is
`0xf674`, or port 63092. The RX3 then opens two substantial TCP connections to
that returned port.

The RX3 also queries rekordbox's UDP port 50111 using RPC port-mapper messages.
It discovers mountd on UDP 57929, mounts `/`, discovers NFS program 100003
version 2 on UDP 2049, and reads the exported library through NFSv2. The NFS
phase contains 1,232 requests and replies as large as 8,292 bytes, including IP
fragmentation.

## First relay failure

The first prototype reached `LinkDeviceManager`, but the registered peer's
UTF-16 name field remained empty and no source appeared in the RX3 UI. Its
behavior differed from the direct session in several material ways:

- it translated only rekordbox type `0x06`, leaving type `0x02` and every RX3
  embedded address inconsistent with the relay endpoints;
- it forged an RX3 announcement whose bytes 36, 37, and 52 differed from the
  stock packet;
- it duplicated and burst claim packets, replayed negotiation after completion,
  and continued sending type `0x02` as a keepalive;
- it did not preserve the type `0x10`/`0x11` peer-registration exchange that
  supplies the computer name;
- its port-12523 proxy passed the returned port 63092 through without creating
  a listener for it;
- it dropped the RPC, mountd, NFS, and fragmented UDP data plane;
- its bootstrap wrote the USB-mounted flag and forced a system state without
  reproducing the stock mount callback or MIDI activation lease.

These failures explain the partial result: one anonymous internal device was
registered, while the source metadata and library services required for the UI
and browsing never became valid.

## Synchronized relay capture

A synchronized capture on the Mac, server `lan0`, server `rx3lan`, and the RX3
USB NIC located the first missing packet in the routed session. At
16:54:26.024691 UTC the RX3 sent a 36-byte type `0x10` request from
`169.254.100.2` to `169.254.100.1` on UDP 50002. The relay emitted it from
`10.0.0.253` to `10.0.0.119`, and rekordbox answered 5 ms later with a 296-byte
type `0x11` packet containing the UTF-16 name `macbook-air`.

That response appears in the Mac, `lan0`, and `rx3lan` captures, with valid IPv4
and UDP checksums, but never appears on the USB NIC. The pattern repeats 13
times at roughly 5.02-second intervals. The relay logs contain none of those
packets, proving that the loss occurs before the userspace socket receives
them.

The destination Ethernet address is the `rx3lan` macvlan MAC, so Linux assigns
the packet to `rx3lan`. The source `10.0.0.119` has its reverse route through
`lan0`. With `net.ipv4.conf.all.rp_filter=1`, Linux applies strict reverse-path
filtering even when the per-interface value is zero and drops the packet after
AF_PACKET capture but before UDP delivery. The setup now uses loose mode (`2`)
on `lan0`, `rx3lan`, and the USB NIC. Loose mode accepts a source reachable by
another interface while retaining reverse-path validation.

The capture also confirms that identity translation is correct for the packets
that do traverse the relay. Rekordbox's LAN type `0x06` identity uses device ID
`0x29`, Wi-Fi MAC `1c:57:dc:39:00:bb`, and IP `10.0.0.119`; the USB-side copy
uses device ID `0x11`, USB MAC `c8:3d:fc:16:af:9a`, and IP `169.254.100.1`.

## Relay design

The server-side implementation in `tools/rx3_link_export` uses three
mechanisms:

1. A userspace relay copies stock Pro DJ Link broadcasts between interfaces and
   substitutes embedded endpoint addresses and identities.
2. A focused TCP broker proxies the port-12523 query and its returned dbserver
   port. It translates the server identity in the first dbserver response and
   becomes byte-transparent for the remaining library session.
3. One-to-one DNAT/SNAT routes RPC, mountd, NFS, and the remaining unicast UDP
   data plane. Conntrack carries runtime-selected mountd ports and NFS
   fragments.

The same process writes the captured activation SysEx to the RX3 raw-MIDI
endpoint every 200 ms. In its normal mode it does not cache or replay handshake
packets, synthesize claims, choose an RX3 device ID, forge announcements, or
patch application state. rekordbox and the RX3 execute their stock negotiation
over the routed path.

## LAN relay identity and UI state

The Mac version of rekordbox uses different Pro DJ Link identities on Wi-Fi and
on the RX3 USB network interface. A live Wi-Fi announcement selected device ID
`0x29` and advertised MAC `1c:57:dc:39:00:bb`. The direct USB capture selected
device ID `0x11` and advertised the USB host MAC `c8:3d:fc:16:af:9a`. Relaying
the IP packet alone leaves those identities inconsistent. The USB-side packets
must use device ID `0x11`, the USB host MAC, and the translated link-local IP.

The RX3 also has two addresses in the prototype topology. Packets leave through
the added `169.254.100.2` alias, while the stock application continues to
advertise its auto-assigned `169.254.175.153` address. A captured relayed type
`0x06` packet consequently had source `10.0.0.253` but still contained
`169.254.175.153` at payload offset 44. The direct USB capture uses the same
address in both places. The relay now learns and translates the RX3's embedded
address independently of its transport source. The affected packet fields are
type `0x02` offset 36, type `0x05` offset 36, and type `0x06` offset 44.
The kernel data plane likewise accepts both the added `169.254.100.2` alias and
the stock `169.254.175.153` primary address. The application uses the primary
address for dbserver, RPC, mountd, and NFS even when control and Telnet traffic
uses the alias.

This applies to unicast Pro DJ Link traffic in both directions as well as
broadcasts. In particular, the RX3 retries type `0x46` requests to UDP port
50002, and rekordbox answers with type `0x47`. The response's source device ID
appears at offsets 33 and 36. A kernel
DNAT path changes only the IP header, so UDP ports 50000-50002 pass through the
userspace translator. RPC, mountd, and NFS UDP remain on the kernel NAT path.

### Dbserver identity gate

A clean failed browse attempt reached the runtime dbserver port and completed
the five-byte `UInt32(1)` greeting. The RX3 then introduced itself as logical
player `0x0b`. Rekordbox answered with the 32-byte success message below and
the RX3 immediately closed the connection:

```text
11 872349ae 11 fffffffe 10 4000 0f 02
14 00000002 0606 11 00000000 11 00000029
```

The same response in the direct USB capture ends in `11 00000011`, after which
the RX3 continues with menu and track requests. The final typed `UInt32` is the
server device identity. Rekordbox's LAN service reports `0x29`, while the
translated USB session established device `0x11`; the RX3 rejects that mixed
identity before any menu request or NFS read.

The TCP broker binds its Rekordbox-facing connections to `10.0.0.253`, proxies
the exact `RemoteDBServer` query, opens the returned dynamic listener before
returning its port to the RX3, and parses only the first response after the
greeting. It changes the final `UInt32` from the learned LAN identity to
`0x11`, then forwards all later dbserver bytes unchanged. The live test reached
the track browser after this translation.

Firmware 1.19 provides independent memory checks for every stage from peer
discovery to a rendered SOURCE row:

| State | Address or object |
| --- | --- |
| Current browse mode (`12` is SOURCE) | `0x0326f8b8` |
| `ui::Net::instance` pointer | `0x02685fdc` |
| Peer slot ID/name/flags | `net + 0x74/0x78/0xba`, stride `0x48` |
| Rendered SOURCE row count | `0x0551aba0` |
| Rendered row UTF-16 name | `0x0551abca + row * 0x238` |
| SOURCE graphics window handle | `0x0268543c` |

`ui::Net::onLinkConnected` at `0x002bc058` populates the peer slots, and
`ui_CTRL_SOURCE_Set` at `0x002b5ea0` builds rows from the source-list model. In
the successful replay, browse mode was 12, the connected peer slot contained
ID `0x11` and `macbook-air`, and the two rendered rows were `macbook-air` and
`SOFTWARE CONTROL`.

### Peer registration and SOURCE eligibility

The peer registry and the browsable-source model are separate firmware states.
`ui::Net::onLinkConnected` stores at most four peers beginning at `net + 0x74`,
with a `0x48`-byte stride. Each slot contains the link ID at `+0x00`, the
UTF-16 name at `+0x04`, and the connected flag in bit 0 of the byte at `+0x46`.
It then notifies the registered `ui::Net` observers. A populated peer slot only
proves that link registration and naming completed.

`ConvertBrowseUi2Gui` at `0x0027b640` constructs the model later rendered by
`ui_CTRL_SOURCE_Set`. Its rekordbox-PC path tests media type 4 with
`CmnFunc_CmnInfo_GetMountInfo_DeviceDetectFlg` for each allowed device ID:

| Device ID | Getter call | Detect-flag address |
| ---: | ---: | ---: |
| `0x11` | `0x0027cf48` | `0x0325b818` |
| `0x12` | `0x0027d008` | `0x0325bcb0` |
| `0x29` | `0x0027d0c0` | `0x03262658` |
| `0x2a` | `0x0027d114` | `0x03262af0` |
| `0x2b` | `0x0027d168` | `0x03262f88` |
| `0x2c` | `0x0027d194` | `0x03263420` |

A zero return skips that PC entirely. A nonzero return causes the builder to
read `DevicePropertyInfo` and `RBM_DispName`, assign source type 3, and append a
row. For ID `0x11`, those calls are at `0x0027cf5c` and `0x0027cf68`.
`ui_CTRL_SOURCE_Set` only renders the resulting `ui_ListDispData`; it does not
consult the peer registry.

The getter and setter at `0x00180458` and `0x00180424` address a detect flag as:

```text
0x03253564 + 0x31ac + device_id * 0x498 + media_type * 0xbc
```

`DevicePropertyInfo` uses the corresponding address
`0x03256714 + device_id * 0x498 + media_type * 0xbc`; ID `0x11`, media 4 is
`0x0325b81c`. The display-name getter at `0x00181274` follows the per-device
pointer at `0x03253564 + device_id * 0x498 + 0x3614` and returns that allocation
plus four bytes.

The NFS device-property receive path explains how the flag becomes nonzero.
`CmnFunc_CmnInfo_NfsDeviceProperty_Receive` at `0x0018284c` stores the property
and sends main-mailbox message `0x3f3`. `Total_MainLinkMessageProc` handles it
at `0x000f8660` and calls `SetMountInfo_DeviceDetectFlg(device, media, 1)` at
`0x000f866c`. A later PC-ready message, `0x1393`, sets media 4 to state 2 at
`0x000f82c8`. The SOURCE predicate accepts either state because it checks only
for zero versus nonzero.

The prerequisite request observed before this state transition is type `0x30`,
rather than type `0x46`. `TotalUdpVola_NfsDeviceProperty_GetReq` at
`0x001dff3c` constructs a type `0x30` datagram for UDP port 50002 and transmits
it at `0x001e0028`. In the direct successful USB capture, rekordbox answers the
second `0x30` request with type `0x31`. The failed routed session emitted one
`0x30`, received no `0x31`, and retained zero PC detect flags. The exact
downstream dispatch from `0x31` to the proven device-property setter chain has
not yet been traced.

Types `0x46` and `0x47` belong to the remote track-load exchange. The firmware
packet table maps outer type `0x47` to `OperatingManager::operatePacket` case
15 at `0x0039880c`, which calls callback slot `+0x20`; for `NetworkManager`
that slot is the `loadMusic` thunk at `0x0039077c`. Type `0x49`, case 16 at
`0x00398870`, calls callback slot `+0x24`, the `receiveDeviceProperty` thunk at
`0x003906c4`. This C++ device-property callback also forwards to
`CmnFunc_CmnInfo_NfsDeviceProperty_Receive`, but it is distinct from the
observed `0x30`/`0x31` request-response path. Repeated `0x46` with no `0x47`
therefore explains a failed track-load operation after selection; it is not the
predicate which keeps the peer out of the SOURCE list.
