<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 settings in the firmware 1.19 Utility screen

## Intended menu

The S3 module owns one section in the long-press Utility screen:

| Row | Source | Interaction |
| --- | --- | --- |
| Wi-Fi status | S3 association state and RSSI | Read-only initially; reconnect later |
| SSID | S3 active/configured network | Open network picker or keyboard |
| Password | S3 credential state | Open masked keyboard; never display the secret |
| Link IP | Address leased to the RX3 `usb0` interface | Read-only |
| Adapter MAC | MAC shared by the S3 Wi-Fi station and USB NCM | Read-only |
| Renew Link address | RX3 DHCP client | Action row |

The transparent bridge does not have a separate IP identity. **Link IP** is the
address assigned to the RX3 through the bridge. **Adapter MAC** is the one MAC
used on both the NCM and Wi-Fi sides.

PCM capture and stream settings can be registered under the same section later.
They remain independent of the Wi-Fi control protocol.

## Measured Utility table ABI

Firmware 1.19 stores the Utility model at `0x005140b8`: a 32-bit count followed
by 33 descriptors of `0x38` bytes each. The table ends at `0x005147f4`, where
unrelated data begins, so appending descriptors in place would corrupt the
firmware image.

The broker clones the complete stock table into its shared object, appends rows,
and guarded-patches seven literal words used by the Utility state machine:

```text
0x0013c9a8  Utility::Init
0x0013cbcc  Utility::StartEdit
0x0013cd1c  Utility::ChangeItem
0x0013cf30  Utility::FinishEdit
0x0013cfc4  Utility::ResetEdit
0x0013d9e4  Utility::SetDispUtilityList direct row-array pointer
0x0013d9ec  Utility::SetDispUtilityList count/table pointer
```

The six count/table words must still contain `0x005140b8`, and the direct row
word must contain `0x005140bc`, before the broker writes anything. A failed
guard restores every word already changed. This gives the core one owner for
the firmware redirection; feature modules only register descriptors.

The direct row pointer is load-bearing. `UiBrowse_SetDispUtilityList` uses it on
its normal display path rather than deriving the rows from the count address.
Redirecting only the other six words makes the list advertise the extended
count while still indexing the 33-row stock array. Reaching the first appended
row then interprets unrelated firmware data as a descriptor and calls an
invalid function pointer.

The first proof clones the stock General section descriptor and the read-only
Version row descriptor. Its value callback fills the firmware's native UTF-16
line objects, so rendering, scrolling, and touch selection continue through the
stock Utility implementation.

Run `tools/rx3_runtime/inspect_utility_table.py` against the decrypted `rbp`
ELF to reproduce the 33-row map and callback addresses.

## Editing constraints

The stock browse keyboard is available through these named firmware functions:

```text
BrowseUiIf::InputKeyboard               0x000cfe70
UiBrowseComm_Keyboard_StartSearchInput  0x0010bee0
BrowseKeyboardUpdate                    0x00121184
BrowseKeyboardFix                       0x001211bc
```

Its buffer accepts 32 characters plus the terminator, which exactly covers the
802.11 SSID limit. A WPA passphrase can contain 63 characters. Password entry
therefore needs a larger broker-owned buffer or a staged input flow before it
is safe to expose as an editable row. Submission also needs one central input
hook that dispatches to the row currently being edited.

## S3 state and control

The existing private EtherType `0x88b5` request `RX3STAT?` already returns
association, NCM carrier, transmit counters, and RSSI without requiring an IP
address. Extend that protocol with versioned commands rather than doing socket
or file I/O in the UI callback:

```text
RX3CFG?                 status, SSID, credential-present flag, MAC
RX3SCAN?                begin/return bounded scan results
RX3SET SSID=...         stage an SSID
RX3SET PASS=...         stage a password
RX3APPLY                 save to NVS and reconnect
```

A low-priority RX3 worker owns those exchanges and publishes a small cached
snapshot. Utility callbacks only copy cached strings. The password travels only
in the control request, is stored by the S3 in NVS, and is represented in the
menu as `SET` or `NOT SET`.

The RX3 owns DHCP. **Renew Link address** should signal the existing DHCP client
or run the same guarded renewal path used by the S3 Link module after NCM carrier
returns. **Reconnect Wi-Fi** is a separate S3 command.

## Staged implementation

1. Display a static section and value through the shared broker.
2. Poll `RX3STAT?` on a worker and show live Wi-Fi state, RSSI, Link IP, and MAC.
3. Add SSID editing with the stock 32-character keyboard.
4. Add safe 63-character password input and masked credential state.
5. Add scan, reconnect, and DHCP-renew action rows.

Each stage leaves the stock 33 rows byte-for-byte cloned and removes the table
redirection when the shared object unloads.
