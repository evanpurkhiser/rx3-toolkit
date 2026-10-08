#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

[ "$2" = "CONNECTED" ] || exit 0
[ -p "$USB_WIFI_EVENT_FIFO" ] || exit 1

printf '%s\n' "$2" > "$USB_WIFI_EVENT_FIFO"
