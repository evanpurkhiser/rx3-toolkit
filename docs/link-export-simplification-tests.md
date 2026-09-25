<!-- SPDX-License-Identifier: MPL-2.0 -->
# Link Export subtraction tests

The working server proxy is a reference implementation, not yet the minimum
implementation. These experiments remove one assumption at a time while
preserving a known-good recovery path. Their main purpose is to separate the
requirements of the routed server topology from the requirements of a future
transparent ESP32-S3 bridge.

## Test discipline

RX3 and rekordbox retain peer and failure state, so every result uses the same
cold procedure:

1. Cold-boot the RX3.
2. Fully quit and reopen rekordbox in Export mode.
3. Tear down and recreate the server network rules.
4. Start simultaneous `lan0`, `rx3lan`, and USB captures before the relay.
5. Start the relay in normal mode without packet replay or RX3 emulation.
6. Enable LINK once, open SOURCE, select the Mac, and browse a track list.
7. Run every candidate twice. After a failure, restore that one feature and
   reproduce the baseline before testing another candidate.

A passing session reaches all of these checkpoints:

| Stage | Evidence |
| --- | --- |
| RX3 discovery | RX3 type `0x06` appears on the LAN |
| Rekordbox discovery | Rekordbox type `0x06` reaches USB |
| Peer registration | `0x10`/`0x11`, populated peer slot and computer name |
| SOURCE eligibility | `0x30`/`0x31`, matching nonzero `pc_detect`, rendered row |
| Database | port 12523 query, greeting, introduction, success, first menu request |
| Library transport | RPC port mapping, mountd, NFSv2 reads |
| User result | folders and a track list are visible |

Track load and playback remain a separate checkpoint until explicitly tested.

## Server identity and transport

Run these in order because later removals depend on earlier results.

### 1. Preserve LAN device `0x29` end to end

Disable both device-ID translations as one experiment:

- forward Rekordbox's UDP identity as `0x29` instead of changing it to `0x11`;
- leave the first dbserver success response at `0x29` instead of changing it
  to `0x11`.

Keep IP and MAC translation, the TCP broker, and all NAT rules. A passing
session should populate peer and media state for device `0x29`, then continue
past the dbserver response into menu requests. Firmware 1.19 explicitly admits
PC IDs `0x29` through `0x2c` in the SOURCE builder.

The two translations must change together. The already-proven mixed state,
UDP `0x11` plus dbserver `0x29`, causes the RX3 to close the database connection.

### 2. Replace the TCP broker with one-to-one NAT

Only attempt this after the coherent `0x29` session passes. Route TCP in both
directions with the same virtual endpoint mapping used by UDP. Port 12523
returns the runtime dbserver port unchanged, and conntrack routes the subsequent
connection without parsing its stream.

Passing removes the dbserver parser, dynamic listeners, learned device-ID
coupling, and listener lifetime state. On failure, compare the TCP source
address and the interval between returning the port and the RX3's next SYN.

### 3. Scope or remove MAC translation

First replace broad whole-payload substitution with translations at the known
packet fields and compare output byte-for-byte against the successful capture.
Then test whether assigning the natural endpoint MAC at layer 2 makes payload
MAC substitution unnecessary. Removing payload rewriting while leaving a
contradictory layer-2 identity is not a useful test.

### 4. Remove the macvlan

After identity translation is reduced, place `10.0.0.253` directly on `lan0`
and send through the server's ordinary LAN interface. A passing session removes
the separate `rx3lan` interface and likely removes the strict reverse-path
asymmetry that required loose `rp_filter` mode.

### 5. Remove fixed address assumptions

The relay already learns the RX3's UDP source. Extend that to the kernel path
so `RX3_PRIMARY_IP=169.254.175.153` is not a captured-session constant. Learn
or constrain the peer to the isolated USB interface and link-local subnet.
Rekordbox's LAN address and endpoint MAC can likewise be learned from the
accepted discovery peer after an explicit single-peer selection policy.

Embedded IP translation remains structurally necessary while the server joins
two different IPv4 subnets. A transparent S3 bridge places both peers on the
same subnet and removes that requirement instead of emulating it.

## RX3 runtime additions

The stock firmware already contains discovery, SOURCE UI, dbserver, RPC,
mountd, NFS, browsing, and track-load behavior. Current tests add only a root
shell/address module and a guarded preload around that stock implementation.

Run these independently from the server transport reductions:

1. Remove the preload's conditional LinkStop-to-Discovery and
   LinkStop-to-Connecting calls, retaining only
   `PcController::handleUsbMountMessage(event=3)`.
2. Omit the empty SysEx and MIDI-CI initialization while retaining the 200 ms
   activation heartbeat.
3. Disable the activation heartbeat for at least ten seconds while retaining
   the stock mounted callback. Observe the certification byte and Link state.
4. Relaunch an unmodified `rbp` without `LD_PRELOAD`, retaining MIDI and every
   server component. This tests whether the Linux host produces the stock USB
   mounted transition by itself.
5. Stop using `169.254.100.2` for protocol traffic and use the learned stock
   primary. Keep the alias temporarily for Telnet observation, then remove the
   root-shell module only after a passing session.
6. Cold-boot with no mod stick and repeat using only screen and packet evidence.

The live bootstrap log shows that the current preload invoked the stock mounted
callback and then resumed both Discovery and Connecting from LinkStop. That is
evidence those calls are active today, not proof that they are irreducible.

## Low-risk code removal

The successful relay runs with `--emulate-rx3` disabled. Its synthetic claim,
status, fallback-announcement, and operating-transition machinery never belongs
to the normal path. Remove it after a baseline capture or move it into a
separate replay diagnostic so production behavior cannot activate it.

The `rx3_announced` receive gate is also suspect. Stock peers retry their
announcements, and forwarding early Rekordbox packets may be safer than dropping
them while USB startup is slow. Test its removal while watching for feedback
loops and duplicates.

Firewall-rule subtraction is host cleanup rather than a protocol result. Its
outcome depends on the server's ambient FORWARD policy, so it should follow the
topology work instead of being treated as evidence about the RX3.
