#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Installs an isolated telemetry preload for one verified RX3 1.19 rbp build.

module_begin usb-telemetry usb_telemetry

USB_TELEMETRY_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
USB_TELEMETRY_SRC=/mnt/iso/modules/usb-telemetry/librx3_usb_telemetry.so
USB_TELEMETRY_LIB=/root/pdj/librx3_usb_telemetry.so
USB_TELEMETRY_TMP=/root/pdj/.librx3_usb_telemetry.so.$$
USB_TELEMETRY_READY=/tmp/rx3-usb-telemetry.ready
USB_TELEMETRY_LOG=/tmp/rx3-usb-telemetry.log
USB_TELEMETRY_RESIDENT=0

register_ready_file "$USB_TELEMETRY_READY"
register_diagnostic_file "$USB_TELEMETRY_LOG"
register_runtime_preload "$USB_TELEMETRY_LIB"

usb_telemetry_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *)   entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$USB_TELEMETRY_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$USB_TELEMETRY_LIB:$cleaned"
    else
        RBP_PRELOAD=$USB_TELEMETRY_LIB
    fi
}

usb_telemetry_prepare()
{
    [ "$ACCEPTED" = "$USB_TELEMETRY_VERIFIED_SHA1" ] || {
        say "USB telemetry disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$USB_TELEMETRY_SRC" ] || {
        say "USB telemetry disabled: shared object is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    usb_telemetry_normalize_preload
    if preload_contains "$USB_TELEMETRY_LIB" &&
       cmp -s "$USB_TELEMETRY_SRC" "$USB_TELEMETRY_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        USB_TELEMETRY_RESIDENT=1
        say "USB telemetry already active, rbp left untouched"
        return 0
    fi

    rm -f "$USB_TELEMETRY_READY" "$USB_TELEMETRY_LOG" "$USB_TELEMETRY_TMP"
    cp "$USB_TELEMETRY_SRC" "$USB_TELEMETRY_TMP" 2>/dev/null || return 1
    chmod 644 "$USB_TELEMETRY_TMP"
    mv -f "$USB_TELEMETRY_TMP" "$USB_TELEMETRY_LIB" 2>/dev/null || {
        rm -f "$USB_TELEMETRY_TMP"
        return 1
    }

    request_rbp_restart
    say "USB telemetry prepared: event-driven rear USB-B HID, protocol v1"
}

usb_telemetry_after_launch()
{
    if [ "$USB_TELEMETRY_RESIDENT" = "1" ]; then
        say "OK: USB telemetry remains active from the previous insertion"
    elif [ -s "$USB_TELEMETRY_READY" ]; then
        say "OK: USB telemetry active"
    else
        say "WARNING: rbp is active but USB telemetry is inactive"
    fi
    [ -r "$USB_TELEMETRY_LOG" ] && cat "$USB_TELEMETRY_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook usb_telemetry_prepare
register_after_launch_hook usb_telemetry_after_launch
