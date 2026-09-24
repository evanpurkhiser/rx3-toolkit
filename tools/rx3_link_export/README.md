# RX3 Link Export relay

This tool places an RX3 USB-network connection onto the rekordbox computer's
LAN without changing the Pro DJ Link, dbserver, mountd, or NFS protocols.

The direct macOS-to-RX3 capture shows three parts to a working session:

1. A Pioneer SysEx activation message is sent over USB MIDI about every 200 ms.
2. Pro DJ Link discovery runs over UDP ports 50000-50002.
3. The peers use unicast TCP and UDP services selected at runtime. In the
   captured session these included TCP 12523, TCP 63092, UDP 50111, UDP 57929,
   and NFSv2 on UDP 2049.

`relay.py` forwards only broadcast discovery and rewrites embedded endpoint
addresses. `setup-nat.sh` installs a one-to-one path for all unicast traffic,
so runtime-selected service ports pass through conntrack without protocol
specific proxies. The relay also maintains the stock USB-MIDI PC-control gate.

## Run

Connect the RX3 USB-B port to the server. Determine the new USB network
interface and both endpoint addresses, then run:

```sh
sudo env \
  USB_INTERFACE=enp0s20f0u9u1c2 \
  REKORDBOX_IP=10.0.0.119 \
  RX3_IP=169.254.175.153 \
  tools/rx3_link_export/setup-nat.sh

sudo python3 -m tools.rx3_link_export.relay \
  --usb-interface enp0s20f0u9u1c2 \
  --rekordbox-ip 10.0.0.119 \
  --rx3-ip 169.254.175.153
```

Start rekordbox Export mode. The RX3 announcement should make the **LINK**
button appear in rekordbox. Click it, then open **SOURCE** on the RX3 and select
the rekordbox computer.

The default LAN-side virtual RX3 address is `10.0.0.253`; the default USB-side
virtual rekordbox address is `169.254.100.1`. Override these values with the
corresponding environment variables and command-line options if either address
is already in use.

The NAT rules are restricted to the selected rekordbox and RX3 addresses. Remove
them with the same environment values after stopping the relay:

```sh
sudo env \
  USB_INTERFACE=enp0s20f0u9u1c2 \
  REKORDBOX_IP=10.0.0.119 \
  RX3_IP=169.254.175.153 \
  tools/rx3_link_export/teardown-nat.sh
```

## Why the first relay failed

The earlier prototype forged part of device discovery and proxied only TCP
12523. A real session establishes substantially more state:

- rekordbox's type `0x02` and RX3's types `0x02`, `0x05`, and `0x06` contain
  endpoint addresses that must agree with the routed addresses;
- type `0x11` carries the rekordbox computer name used by the RX3 source UI;
- TCP 12523 returns the runtime dbserver port rather than carrying the library;
- RPC port mapping selects mountd dynamically, followed by NFSv2 traffic;
- `f0 00 40 05 00 00 03 0d 00 50 01 f7` keeps the RX3 PC-control certificate
  active and expires after about one second without another message.

The prototype also bound type `0x29` sends to an ephemeral port despite taking
a requested source-port argument, left RX3 embedded addresses untranslated,
and repeatedly replayed claim packets that appear only during initial device-ID
negotiation. Relaying the stock packets and routing all unicast traffic avoids
those synthetic state mismatches.
