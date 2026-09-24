<!-- SPDX-License-Identifier: MPL-2.0 -->
# Firmware 1.19 Link Export

This document describes a successful direct USB Link Export session between
rekordbox on macOS and an XDJ-RX3 running firmware 1.19. It also records why the
first LAN-to-USB relay prototype reached internal device discovery without
showing a usable source in the RX3 UI.

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
- it did not preserve the type `0x11` device-property exchange that supplies
  the computer name;
- its port-12523 proxy passed the returned port 63092 through without creating
  a listener for it;
- it dropped the RPC, mountd, NFS, and fragmented UDP data plane;
- its bootstrap wrote the USB-mounted flag and forced a system state without
  reproducing the stock mount callback or MIDI activation lease.

These failures explain the partial result: one anonymous internal device was
registered, while the source metadata and library services required for the UI
and browsing never became valid.

## Relay design

The server-side implementation in `tools/rx3_link_export` uses two mechanisms:

1. A userspace relay copies stock Pro DJ Link broadcasts between interfaces and
   substitutes only embedded endpoint addresses.
2. One-to-one DNAT/SNAT routes all unicast protocols and ports between the two
   peers. Conntrack carries runtime-selected dbserver and mountd ports, NFS
   fragments, and connections initiated in either direction.

The same process writes the captured activation SysEx to the RX3 raw-MIDI
endpoint every 200 ms. It does not synthesize claims, choose a device ID, forge
announcements, or patch application state. rekordbox and the RX3 execute their
stock negotiation over the routed path.
