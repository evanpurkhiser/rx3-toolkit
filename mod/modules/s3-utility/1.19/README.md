<!-- SPDX-License-Identifier: MPL-2.0 -->
# ESP32-S3 utility integration

Adds an **ESP32-S3 INTEGRATION** section to the RX3 long-press Utility screen.
The initial firmware proof contributes a read-only Wi-Fi status row through the
shared UI core. It demonstrates that stock Utility rows can be extended without
replacing the screen renderer or intercepting its input path.

Select this module when building `autoexec.bin`, boot the RX3 with that payload,
then hold **MENU / UTILITY**. The new section appears below the stock General
settings.

The proof value is static. Reading S3 state, editing credentials, asking the S3
to scan or reconnect, and renewing the RX3 Link address still require the
control protocol and background state cache described in
`docs/s3-utility-menu-1.19.md`.
