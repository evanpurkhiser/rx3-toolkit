#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Starts one passive, bounded rekordbox USB and Link Ethernet capture.

module_begin rekordbox-capture rekordbox_capture

REKORDBOX_CAPTURE_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
REKORDBOX_CAPTURE_SRC=/mnt/iso/modules/rekordbox-capture/librx3_rekordbox_capture.so
REKORDBOX_CAPTURE_LIB=/root/pdj/librx3_rekordbox_capture.so
REKORDBOX_CAPTURE_TMP=/root/pdj/.librx3_rekordbox_capture.so.$$
REKORDBOX_CAPTURE_ETH_SRC=/mnt/iso/modules/rekordbox-capture/rx3-eth0-capture
REKORDBOX_CAPTURE_ETH=/tmp/rx3-eth0-capture
REKORDBOX_CAPTURE_OUT=$USB/RX3_CAPTURE
REKORDBOX_CAPTURE_JSON=$REKORDBOX_CAPTURE_OUT/rekordbox-usb.jsonl
REKORDBOX_CAPTURE_PCAP=$REKORDBOX_CAPTURE_OUT/eth0.pcap
REKORDBOX_CAPTURE_READY=/tmp/rx3-rekordbox-capture.ready
REKORDBOX_CAPTURE_PCAP_READY=/tmp/rx3-eth0-capture.ready
REKORDBOX_CAPTURE_LOG=/tmp/rx3-rekordbox-capture.log
REKORDBOX_CAPTURE_PCAP_LOG=/tmp/rx3-eth0-capture.log
REKORDBOX_CAPTURE_RESIDENT=0

register_ready_file "$REKORDBOX_CAPTURE_READY"
register_ready_file "$REKORDBOX_CAPTURE_PCAP_READY"
register_diagnostic_file "$REKORDBOX_CAPTURE_LOG"
register_diagnostic_file "$REKORDBOX_CAPTURE_PCAP_LOG"
register_runtime_preload "$REKORDBOX_CAPTURE_LIB"

rekordbox_capture_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *) entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$REKORDBOX_CAPTURE_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$REKORDBOX_CAPTURE_LIB:$cleaned"
    else
        RBP_PRELOAD=$REKORDBOX_CAPTURE_LIB
    fi
}

rekordbox_capture_prepare()
{
    [ "$ACCEPTED" = "$REKORDBOX_CAPTURE_VERIFIED_SHA1" ] || {
        say "Rekordbox capture disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$REKORDBOX_CAPTURE_SRC" ] && [ -x "$REKORDBOX_CAPTURE_ETH_SRC" ] || {
        say "Rekordbox capture disabled: a device component is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    rekordbox_capture_normalize_preload
    if preload_contains "$REKORDBOX_CAPTURE_LIB" &&
       cmp -s "$REKORDBOX_CAPTURE_SRC" "$REKORDBOX_CAPTURE_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ] &&
       [ -s "$REKORDBOX_CAPTURE_READY" ] &&
       [ -s "$REKORDBOX_CAPTURE_PCAP_READY" ]; then
        REKORDBOX_CAPTURE_RESIDENT=1
        say "Rekordbox capture already active, existing files preserved"
        return 0
    fi

    mkdir -p "$REKORDBOX_CAPTURE_OUT" 2>/dev/null || {
        say "Rekordbox capture disabled: $REKORDBOX_CAPTURE_OUT is not writable"
        return 1
    }
    test_file=$REKORDBOX_CAPTURE_OUT/.write-test.$$
    : > "$test_file" 2>/dev/null || {
        say "Rekordbox capture disabled: $REKORDBOX_CAPTURE_OUT is not writable"
        return 1
    }
    rm -f "$test_file" "$REKORDBOX_CAPTURE_JSON" "$REKORDBOX_CAPTURE_PCAP" \
        "$REKORDBOX_CAPTURE_READY" "$REKORDBOX_CAPTURE_PCAP_READY" \
        "$REKORDBOX_CAPTURE_LOG" "$REKORDBOX_CAPTURE_PCAP_LOG"

    cp "$REKORDBOX_CAPTURE_SRC" "$REKORDBOX_CAPTURE_TMP" 2>/dev/null || return 1
    chmod 644 "$REKORDBOX_CAPTURE_TMP"
    mv -f "$REKORDBOX_CAPTURE_TMP" "$REKORDBOX_CAPTURE_LIB" 2>/dev/null || {
        rm -f "$REKORDBOX_CAPTURE_TMP"
        return 1
    }
    cp "$REKORDBOX_CAPTURE_ETH_SRC" "$REKORDBOX_CAPTURE_ETH" 2>/dev/null || return 1
    chmod 700 "$REKORDBOX_CAPTURE_ETH" || return 1

    export RX3_REKORDBOX_CAPTURE_PATH=$REKORDBOX_CAPTURE_JSON
    request_rbp_restart
    say "Rekordbox capture armed: 64 MiB USB log and 128 MiB eth0 PCAP"
}

rekordbox_capture_start_ethernet()
{
    [ "$REKORDBOX_CAPTURE_RESIDENT" = "0" ] || return 0
    "$REKORDBOX_CAPTURE_ETH" eth0 "$REKORDBOX_CAPTURE_PCAP" \
        "$REKORDBOX_CAPTURE_PCAP_READY" "$REKORDBOX_CAPTURE_PCAP_LOG" &
    capture_pid=$!
    echo "$capture_pid" > /tmp/rx3-eth0-capture.pid
    kill -0 "$capture_pid" 2>/dev/null || {
        say "Rekordbox capture disabled: eth0 capture did not start"
        return 1
    }
}

rekordbox_capture_after_launch()
{
    [ -s "$REKORDBOX_CAPTURE_READY" ] && [ -s "$REKORDBOX_CAPTURE_PCAP_READY" ] || {
        say "WARNING: rbp is active but the complete capture is inactive"
        return 1
    }
    if [ "$REKORDBOX_CAPTURE_RESIDENT" = "1" ]; then
        say "OK: existing rekordbox capture remains active"
    else
        say "OK: recording rekordbox USB and eth0 to $REKORDBOX_CAPTURE_OUT"
    fi
}

register_prepare_hook rekordbox_capture_prepare
register_stopped_hook rekordbox_capture_start_ethernet
register_after_launch_hook rekordbox_capture_after_launch
