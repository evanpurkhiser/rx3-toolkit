<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 utility integration

Adds an **ESP32-S3 INTEGRATION** section to the RX3 long-press Utility screen.
It shows live Wi-Fi state and RSSI, SSID, password-presence state, the RX3 Link
IP, and the shared Wi-Fi/NCM MAC.

Select this module when building `autoexec.bin`, boot the RX3 with that payload,
then hold **MENU / UTILITY**. The new section appears below the stock General
settings.

The injected library runs one background worker. It exchanges versioned private
Ethernet frames with the S3 once per second and publishes a small locked cache.
Utility callbacks only copy cached values; they never perform USB or network
I/O. The control plane remains available before Wi-Fi association and without a
DHCP lease.

The payload also installs `/root/pdj/rx3-s3-config`. From an RX3 root shell:

```sh
/root/pdj/rx3-s3-config usb0 status
/root/pdj/rx3-s3-config usb0 set 'network name'
/root/pdj/rx3-s3-config usb0 reconnect
```

`set` prompts for the password with terminal echo disabled. It also accepts the
password on standard input for automation. The password is never passed in an
argument, printed, logged, or returned by the S3. An empty password selects an
open network; WPA passphrases must contain 8–63 bytes.

SSID/password editing in the Utility screen remains a later stage. The stock
keyboard safely holds a 32-byte SSID, but password entry needs a broker-owned
65-byte masked modal and central submit/cancel interception. The command above
exercises the complete write path without coupling credential transport to that
firmware-specific UI work.
