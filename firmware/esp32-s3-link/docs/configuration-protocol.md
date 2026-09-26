# S3 configuration protocol

The RX3 configures and monitors the S3 through Ethernet frames on the existing
CDC-NCM interface. Configuration traffic uses experimental EtherType `0x88b5`
and never leaves the point-to-point USB link. The protocol is independent of
ESP-IDF and its constants live in `include/rx3_s3_config_protocol.h` so the S3,
RX3 module, and host diagnostic tool share one definition.

Every protocol payload starts with this 10-byte header:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII `RX3C` |
| 4 | 1 | Opcode; responses set bit 7 |
| 5 | 1 | Status; requests set it to zero |
| 6 | 2 | Request ID, unsigned network byte order |
| 8 | 2 | Payload length, unsigned network byte order |

Ethernet minimum-frame padding follows the declared payload and is ignored.
Responses echo the request ID. Malformed headers, unknown opcodes, and invalid
command payloads receive `BAD_REQUEST` when enough of the header is available
to identify the exchange.

## Commands

`GET_STATUS` (`1`) has an empty request. Its response contains flags (one
byte), signed RSSI (one byte, `-127` when unavailable), the shared six-byte
Wi-Fi/NCM MAC, SSID length (one byte), and the raw SSID (zero through 32
bytes). Flags report Wi-Fi association and whether the configured password is
set. NCM carrier is always up while the USB function is available, so it is not
repeated in the response. The password is never returned.

`SET_CREDENTIALS` (`2`) contains SSID length, password length, the raw SSID,
and the raw password. SSIDs are 1–32 bytes. Passwords are either empty for an
open network or 8–63 bytes. The complete pair is validated and passed to
ESP-IDF's persistent station configuration. A successful response is queued
before association is restarted; the NCM control carrier remains available
throughout.

`RECONNECT` (`3`) has an empty request. Its successful response is queued
before the station reconnect begins.

At boot, the firmware uses ESP-IDF's stored station configuration. When no SSID
has been stored, the ignored local sdkconfig credentials initialize it.

## Status values

| Value | Name |
| ---: | --- |
| 0 | `OK` |
| 1 | `BAD_REQUEST` |
| 2 | `FAILED` |

The RX3 obtains its Link IP from its own `usb0` interface. The transparent S3
does not own an IP address, so the S3 status response deliberately has no IP
field. NCM carrier stays up even while Wi-Fi is disconnected, keeping this API
reachable when credentials are invalid. Transparent LAN forwarding remains
gated by Wi-Fi association, and the RX3 renews DHCP once association succeeds.
