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

Each native descriptor has this measured layout:

| Offset | Field | Callback contract |
| --- | --- | --- |
| `0x00` | UTF-16 label pointer | |
| `0x04` | UTF-16 value-pointer array | |
| `0x08` | Value count | |
| `0x0c` | Original/current value | |
| `0x10` | Staged value | Initialized to `-1` |
| `0x14` | Editable flag | |
| `0x18` | Initialize callback | `void initialize(descriptor *)` |
| `0x1c` | Line callback | `int set_line(descriptor *, line_pair *)` |
| `0x20` | Start callback | `int start_edit(descriptor *)` |
| `0x24` | Selection callback | `int select(descriptor *, int change)` |
| `0x28` | Modified callback | `int is_modified(descriptor *)` |
| `0x2c` | Enter callback | `int commit(descriptor *)` |
| `0x30` | Grey callback | `int is_grey(descriptor *)` |
| `0x34` | Reset callback | `int reset(descriptor *)` |

`UiBrowse_InitUtilityItem` at `0x0013c960` calls the initializer for every
descriptor. `UiBrowse_StartUtilityEdit` at `0x0013c9ac` checks the editable
flag and enters the normal selection editor only when the start callback
returns exactly `1`. Change, finish, and reset proceed through
`UiBrowse_ChangeUtilityItem` at `0x0013cbd8`,
`UiBrowse_FinishUtilityEdit` at `0x0013cd24`, and
`UiBrowse_ResetUtilityEdit` at `0x0013cf3c`.

An action row uses a custom start callback which queues work and returns `0`.
The firmware stays outside selection-edit mode and runs the row's reset
callback. The start callback executes on the UI control path, so it must not
perform NCM exchange, DHCP, filesystem access, or waits itself. A worker owns
the operation and publishes the resulting status through the normal cache.

Action descriptors need a valid one-entry value array and a complete set of
reachable callbacks. Changing the read-only Version descriptor's editable bit
is unsafe: a vendor-specific branch in `UiBrowse_StartUtilityEdit` can index a
row's values before calling its start callback.

## Editing constraints

The stock browse keyboard is available through these named firmware functions:

```text
BrowseUiIf::InputKeyboard               0x000cfe70
UiBrowseComm_Keyboard_StartSearchInput  0x0010bee0
BrowseKeyboardUpdate                    0x00121184
BrowseKeyboardFix                       0x001211bc
```

`getKeyboardInfoPointer` at `0x00112684` returns a byte-oriented state object
whose text buffer begins at offset `0x08` and holds 32 bytes plus a terminator.
`UiBrowseComm_Keyboard_InputChar` rejects another character after 32 bytes,
while the clear and copy paths use the same `0x21`-byte bound. This covers an
ASCII SSID of the maximum 32-octet length, although it cannot represent every
binary or UTF-8 SSID.

The keyboard is coupled to the Browse state machine. `keyboardAppear` at
`0x001038a0` requires Browse list 0, line 0 to have type `0x25` and refuses to
open otherwise. On submit, `BrowseKeyboardFix` calls `InputCharEnter` at
`0x00102084`, which writes command `0x21` into the global Browse command state.
Calling `BrowseUiIf::InputKeyboard` directly from a Utility row therefore does
not produce a safe editor.

SSID editing can reuse the stock keyboard through one input-session shim owned
by the core UI broker. The shim records the target field, prepares a controlled
keyboard host, intercepts submit and cancel before the Browse state machine
consumes them, and sends the accepted value to the S3 worker. Feature modules
register the field and callbacks rather than installing their own keyboard
hooks.

Password entry needs a broker-owned custom masked editor. WPA passphrases can
contain 63 characters, and a raw PSK representation can require 64 hexadecimal
characters. Its buffer therefore holds 64 bytes plus a terminator. The stock
firmware contains translated labels for SSID and Password but no reusable
Wi-Fi page or password-masking implementation. Extending the stock keyboard
would require redirecting its global state, patching several independent size
constants, and replacing its display behavior. Until the custom editor exists,
the RX3 displays only `SET` or `NOT SET`; credentials can be written through
the packaged RX3 control command without returning or logging the stored
password.

## S3 state and control

The configuration API uses compact `RX3C` messages under private
EtherType `0x88b5`. It reports association, RSSI, the shared MAC, active SSID,
and whether a password is set without requiring an IP address.
It accepts atomic SSID/password updates and an explicit reconnect request.
The complete wire format lives in
`firmware/esp32-s3-link/docs/configuration-protocol.md`.

A low-priority RX3 worker owns status exchanges and publishes a small locked
snapshot once per second. Utility callbacks only copy cached strings. Losing
the interface or a response clears transient values instead of displaying stale
state. The password travels only in `SET_CREDENTIALS`, is stored by the S3 in
NVS, and is represented in the menu as `SET` or `NOT SET`.

The packaged `/root/pdj/rx3-s3-config` command provides status, credential
updates, and reconnect control while the editable UI is developed. Its password
prompt disables terminal echo and clears the local buffer after the exchange.

The RX3 owns DHCP. **Renew Link address** should signal the existing DHCP client
or run the same guarded renewal path used by the S3 Link module after NCM carrier
returns. **Reconnect Wi-Fi** is a separate S3 command.

## Staged implementation

The shared broker, live background cache, read-only rows, atomic credential
write API, reconnect command, and packaged RX3 command are implemented. The
remaining UI stages are:

1. Add the central input-session shim and use the stock 32-byte keyboard for
   SSID editing.
2. Add the broker-owned 65-byte masked password editor.
3. Add scan, reconnect, and DHCP-renew action rows.

Each stage leaves the stock 33 rows byte-for-byte cloned and removes the table
redirection when the shared object unloads.

## Live validation

The first RX3 test redirected only the six count/table literals. The Utility
screen opened, but `rbp` exited when scrolling reached an appended row because
the normal display path still indexed the stock descriptor array through its
separate `0x0013d9e4` literal. Firmware data following the 33rd stock descriptor
was interpreted as a descriptor and supplied an invalid callback pointer.

The corrected broker guards and redirects all seven literals as one operation.
On September 26, 2026, firmware 1.19 displayed the appended **ESP32-S3
INTEGRATION** section and **WI-FI STATUS** proof row, scrolled through them, and
kept `rbp` running. The live log reported a native item count of 35.
