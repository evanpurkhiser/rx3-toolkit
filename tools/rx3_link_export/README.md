# RX3 Link Export relay

This tool places an RX3 USB-network connection onto the rekordbox computer's
LAN. It translates the network identity while preserving the Pro DJ Link,
dbserver, mountd, and NFS protocols.

The direct macOS-to-RX3 capture shows three parts to a working session:

1. A Pioneer SysEx activation message is sent over USB MIDI about every 200 ms.
2. Pro DJ Link discovery runs over UDP ports 50000-50002.
3. The peers use unicast TCP and UDP services selected at runtime. In the
   captured session these included TCP 12523, TCP 63092, UDP 50111, UDP 57929,
   and NFSv2 on UDP 2049.

`relay.py` forwards Pro DJ Link UDP ports 50000-50002 and rewrites embedded
endpoint addresses and identities. Its TCP broker handles the
`RemoteDBServer` query and the returned dynamic port, translating the first
dbserver response from rekordbox's LAN device ID to its USB device ID. Later
library messages pass through byte-for-byte. `setup-nat.sh` routes RPC, mountd,
NFS, and other unicast UDP traffic through conntrack. The relay also maintains
the stock USB-MIDI PC-control gate.

The LAN-facing RX3 address belongs to a macvlan interface. `setup-nat.sh` puts
the relay interfaces in loose reverse-path-filter mode because replies from the
rekordbox computer enter `rx3lan` even though their reverse route uses `lan0`.
Strict filtering discards those replies before they reach the relay socket.

The RX3 is one network appliance with one MAC and IPv4 address. Its two decks
are logical players, identified as `0x0b` and `0x0c` in captured status packets;
they are not represented as two virtual CDJs or two IP addresses. The relay
does not cache, replay, or synthesize Link packets. Its dbserver state is
limited to the dynamic TCP listener and the first response's device identity.

Linux enumerates the network and MIDI functions without necessarily delivering
the USB-mounted callback that macOS produces. `rx3_link_bootstrap.c` is a
firmware-guarded preload for that case. It calls the stock
`PcController::handleUsbMountMessage` mounted branch once, which initializes
the mixer key map, starts MIDI reception, and lets `NetworkMonitor` observe the
normal mounted flag on its own timer.

The diagnostic `--emulate-rx3` mode can repeat the byte-for-byte stock type
`0x06` announcement recovered from a direct session, substituting only the
routed LAN address and configured RX3 MAC. Normal relay operation leaves this
mode disabled.

## Tested server topology

```text
rekordbox / MacBook                  Linux server                     RX3 1.19
10.0.0.119                           lan0
device ID 0x29  <---- LAN ---->  rx3lan 10.0.0.253
                                  relay.py
                                  USB 169.254.100.1  <---- USB-B ----> eth0
                                                                  169.254.100.2 alias
                                                               169.254.175.153 primary
                                                                  device ID 0x11
```

The server presents one virtual RX3 endpoint to the LAN and one virtual
rekordbox endpoint to the RX3. The two decks remain logical player IDs `0x0b`
and `0x0c` behind the single virtual RX3 address.

| Role | Address or identity | Owner |
| --- | --- | --- |
| Rekordbox LAN endpoint | `10.0.0.119`, device `0x29` | MacBook |
| Virtual RX3 on LAN | `10.0.0.253`, MAC `c8:3d:fc:16:af:99` | `rx3lan` macvlan |
| Virtual rekordbox on USB | `169.254.100.1`, MAC `c8:3d:fc:16:af:9a` | server USB NIC |
| RX3 control alias | `169.254.100.2/16` | USB Link root-shell module |
| RX3 stock primary | `169.254.175.153/16` in the tested session | RX3 link-local setup |
| Rekordbox USB identity | device `0x11` | translated relay identity |

`RX3_PRIMARY_IP` is explicit because `rbp` originates dbserver, RPC, mountd,
and NFS from the stock primary address even when Telnet and relayed control
traffic use the stable alias.

## Component responsibilities

| Component | Responsibility |
| --- | --- |
| USB Link root-shell module | Adds `169.254.100.2/16` and exposes the diagnostic shell |
| `rx3_link_bootstrap.c` | Replays the stock mounted callback after `rbp` starts; resumes Discovery/Connecting only if the application remains in LinkStop |
| Raw USB MIDI loop | Sends the captured host initialization, then refreshes the one-second PC-control lease every 200 ms |
| UDP relay | Forwards ports 50000-50002 and translates embedded IP, MAC, and rekordbox device-ID fields |
| TCP dbserver broker | Proxies port 12523 and its returned dynamic port; normalizes only the first server identity `0x29` to `0x11` |
| nftables/conntrack | Routes RPC, dynamic mountd, NFSv2, and fragmented UDP between the peers |
| `inspect_ui.py` | Reads peer, media-detect, NFS-drive, and rendered SOURCE state over Telnet |

The direct track browser test exercised every row in this table. Source
discovery required a successful UDP `0x30`/`0x31` exchange. Opening the source
then required the dbserver identity translation before the RX3 would continue
with menu requests.

## Traffic paths

| Traffic | Server path | Translation |
| --- | --- | --- |
| UDP 50000-50002 | `relay.py` sockets | IPs, MACs, and rekordbox `0x29`/`0x11` identity |
| TCP 12523 | local dbserver broker | returned port is preserved |
| Runtime dbserver TCP | local dynamic broker listener | final `UInt32` in first response only |
| RPC, mountd, NFSv2 UDP | nftables DNAT/SNAT | IP headers only |
| Fragmented NFS replies | kernel forwarding/conntrack | fragments remain in the kernel data plane |
| USB MIDI | raw ALSA MIDI device | captured initialization and activation SysEx |

The dynamic dbserver port and mountd port change between sessions. No rule or
configuration should hardcode the observed `63092` and `57929` examples.

## RX3-side prerequisite

The tested Linux-host path needs both the stable USB address and the stock
USB-mounted application transition. Load the `usb-link-root-shell` module from
the toolkit drive, then build the firmware-guarded preload in the required
reverse-engineering container:

```sh
tools/rx3_link_export/build-device.sh build/link-export
```

Place `librx3_link_bootstrap.so` at `/tmp/librx3_link_bootstrap.so` on the RX3
and relaunch `/root/pdj/rbp` with it in `LD_PRELOAD`; `relaunch-rbp.sh` records
that exact relaunch sequence. The preload is pinned to firmware 1.19 and exits
without calling firmware functions when its instruction and object guards do
not match. Its log is `/tmp/rx3-link-bootstrap.log`.

## Run

Connect the RX3 USB-B port to the server. Determine the new USB network
interface and the rekordbox computer's LAN address. The tested USB interface
has MAC `c8:3d:fc:16:af:9a`:

```sh
ip -br link | grep -i c8:3d:fc:16:af:9a
```

Verify that `10.0.0.253` is unused on the LAN and `169.254.100.1` is unused on
the USB link. Read the RX3's current primary address with `ip address show eth0`
over Telnet; pass it as `RX3_PRIMARY_IP` when it differs from the captured
`169.254.175.153` default. Then run:

```sh
sudo env \
  USB_INTERFACE=enp0s20f0u9u1c2 \
  REKORDBOX_IP=10.0.0.119 \
  tools/rx3_link_export/setup-nat.sh

python3 -m tools.rx3_link_export.relay \
  --lan-output-interface rx3lan \
  --usb-interface enp0s20f0u9u1c2 \
  --rekordbox-ip 10.0.0.119 \
  --midi-device /dev/snd/midiC0D0
```

Start the relay before relaunching `rbp`, or relaunch it promptly afterward;
the preload waits 15 seconds before issuing the mounted callback. A transient
user service keeps the relay alive without tying it to a terminal:

```sh
systemd-run --user --collect --unit=codex-rx3-link-export \
  --description='RX3 Link Export relay' \
  --working-directory="$PWD" \
  --property=RuntimeMaxSec=1d \
  python3 -m tools.rx3_link_export.relay \
    --lan-output-interface rx3lan \
    --usb-interface enp0s20f0u9u1c2 \
    --rekordbox-ip 10.0.0.119 \
    --midi-device /dev/snd/midiC0D0
```

Start rekordbox Export mode. The RX3 announcement should make the **LINK**
button appear in rekordbox. Click it, then open **SOURCE** on the RX3 and select
the rekordbox computer.

A successful source selection produces these broker messages:

```text
dbserver dynamic port ready: <runtime port>
dbserver identity normalized: 0x29 -> 0x11
```

### Experimental LAN identity mode

The default relay behavior above remains the tested path. To test whether the
RX3 accepts rekordbox's LAN personality consistently across both protocols,
start the relay with `--preserve-rekordbox-device-id`:

```sh
python3 -m tools.rx3_link_export.relay \
  --lan-output-interface rx3lan \
  --usb-interface enp0s20f0u9u1c2 \
  --rekordbox-ip 10.0.0.119 \
  --midi-device /dev/snd/midiC0D0 \
  --preserve-rekordbox-device-id
```

This single flag preserves the learned LAN device ID, normally `0x29`, in both
the translated Pro DJ Link UDP packets and the first dbserver response. IP and
MAC translation, the TCP broker, MIDI activation, and the routed NFS data path
remain active. Keeping both identity surfaces together avoids the known mixed
session where UDP registers device `0x11` but dbserver reports `0x29`.

Use a cold session for the experiment: quit rekordbox, restart the relay, and
restart or relaunch the RX3 application before enabling LINK. A successful
SOURCE state should show peer ID `0x29`, a nonzero `pc_detect['29']` value, and
the rekordbox computer in the rendered rows:

```text
pc_detect={'11': 0, '12': 0, '29': 1, '2A': 0, '2B': 0, '2C': 0}
peer[0] id=41 flags=0x01 name='macbook-air'
row[0] name='macbook-air'
```

The detect value may advance from `1` to `2` as the PC-backed media becomes
ready. The relay should print the dynamic dbserver port without printing
`dbserver identity normalized: 0x29 -> 0x11`. Select the source and open a
track list to prove the preserved identity also passes the dbserver gate.

The reliable cold-start order is:

1. Boot the RX3 with the USB Link root-shell module and connect rear USB-B.
2. Run `setup-nat.sh` and start the relay; wait for `dbserver broker ready` and
   `broadcast relay ready`.
3. Relaunch `rbp` with the preload when the mounted callback is not already
   active.
4. Fully open rekordbox in Export mode and wait for **LINK**.
5. Enable **LINK**, wait for both RX3 deck slots, open **SOURCE**, and select
   the Mac.

Restarting the relay invalidates its learned LAN device identity and dynamic
dbserver listeners. Return to SOURCE and select the Mac again after a relay
restart. Restart rekordbox when it has already rejected an earlier incomplete
session; it can retain the failed peer state until its Link subsystem is
reinitialized.

| Event | Recovery |
| --- | --- |
| Rear USB-B replug changes the Linux interface name | Stop the relay, rerun setup with the new interface, and restart the relay |
| ALSA assigns a different raw-MIDI path | Pass the new `/dev/snd/midiC*D*` path explicitly |
| `rbp` restarts | Wait for the preload sequence, then return to SOURCE and select the Mac |
| Relay restarts | Reselect the source so port 12523 can create a fresh dynamic listener |
| Rekordbox was open during a failed handshake | Fully quit and reopen rekordbox, enable LINK, then reselect the source |
| Mac Wi-Fi disconnects | Restore Wi-Fi; restart rekordbox if its source does not re-register |

With the diagnostic Telnet module active, verify the firmware's current source
model directly:

```sh
python3 tools/rx3_link_export/inspect_ui.py
```

The command reports the connected `ui::Net` peer slots, rendered SOURCE row
names, current browse mode, and SOURCE window handle. `source_active=True` plus
a rendered `macbook-air` row means the application has built that visible tile.
The source row can exist before browsing works. A successful selection also
needs the dbserver connection to remain open and proceed into menu requests.

Use these checks to locate a failure:

```sh
journalctl --user -u codex-rx3-link-export -f
python3 tools/rx3_link_export/inspect_ui.py
ssh-agent-ctx "Capture the RX3 Link Export session" -- \
  sudo tools/rx3_link_export/capture-session.sh
```

The capture helper records `lan0`, `rx3lan`, and the USB NIC. Capture files can
be many gigabytes and stay outside Git.

The USB Link root-shell module gives the RX3 the stable USB-side address
`169.254.100.2`. The default LAN-side virtual RX3 address is `10.0.0.253`, and
the default USB-side virtual rekordbox address is `169.254.100.1`. Override
these values with the corresponding environment variables and command-line
options if an address is already in use. The stock application also originates
dbserver, RPC, and NFS traffic from its primary auto-assigned address,
`169.254.175.153`; the NAT rules accept both RX3 source addresses.

The NAT rules are restricted to the selected rekordbox and RX3 addresses. Remove
them with the same environment values after stopping the relay:

```sh
sudo env \
  USB_INTERFACE=enp0s20f0u9u1c2 \
  REKORDBOX_IP=10.0.0.119 \
  tools/rx3_link_export/teardown-nat.sh
```

Teardown removes the dedicated nftables table, forwarding rules, macvlan, and
temporary addresses. It deliberately leaves global IP forwarding and the loose
per-interface reverse-path settings in their current state because the script
cannot reconstruct values owned by other server services.

## Why the first relay failed

The earlier prototype forged part of device discovery and proxied only TCP
12523. A real session establishes substantially more state:

- rekordbox's type `0x02` and RX3's types `0x02`, `0x05`, and `0x06` contain
  endpoint addresses that must agree with the routed addresses;
- type `0x11` carries the rekordbox computer name used by the RX3 source UI;
- TCP 12523 returns the runtime dbserver port rather than carrying the library;
- rekordbox identifies its LAN dbserver as device `0x29`, while the RX3's USB
  session expects the translated identity `0x11` in the first response;
- RPC port mapping selects mountd dynamically, followed by NFSv2 traffic;
- `f0 00 40 05 00 00 03 0d 00 50 01 f7` keeps the RX3 PC-control certificate
  active and expires after about one second without another message.

The prototype also bound type `0x29` sends to an ephemeral port despite taking
a requested source-port argument, left RX3 embedded addresses untranslated,
and repeatedly replayed claim packets that appear only during initial device-ID
negotiation. Relaying the stock packets, translating the dbserver identity, and
routing the remaining data plane avoids those synthetic state mismatches.
