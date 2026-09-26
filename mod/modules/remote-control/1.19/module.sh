#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Installs a RAM-only bidirectional panel-control bridge.

module_begin remote-control remote_control

REMOTE_CONTROL_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
REMOTE_CONTROL_SRC=/mnt/iso/modules/remote-control/librx3_remote_control.so
REMOTE_CONTROL_LIB=/root/pdj/librx3_remote_control.so
REMOTE_CONTROL_TMP=/root/pdj/.librx3_remote_control.so.$$
REMOTE_CONTROL_READY=/tmp/rx3-remote-control.ready
REMOTE_CONTROL_LOG=/tmp/rx3-remote-control.log
REMOTE_CONTROL_RESIDENT=0

register_ready_file "$REMOTE_CONTROL_READY"
register_diagnostic_file "$REMOTE_CONTROL_LOG"
register_runtime_preload "$REMOTE_CONTROL_LIB"

remote_control_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *)   entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$REMOTE_CONTROL_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$REMOTE_CONTROL_LIB:$cleaned"
    else
        RBP_PRELOAD=$REMOTE_CONTROL_LIB
    fi
}

remote_control_prepare()
{
    [ "$ACCEPTED" = "$REMOTE_CONTROL_VERIFIED_SHA1" ] || {
        say "Remote control disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$REMOTE_CONTROL_SRC" ] || {
        say "Remote control disabled: shared object is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    remote_control_normalize_preload
    if preload_contains "$REMOTE_CONTROL_LIB" &&
       cmp -s "$REMOTE_CONTROL_SRC" "$REMOTE_CONTROL_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        REMOTE_CONTROL_RESIDENT=1
        say "Remote control already active, rbp left untouched"
        return 0
    fi

    rm -f "$REMOTE_CONTROL_READY" "$REMOTE_CONTROL_LOG" "$REMOTE_CONTROL_TMP"
    cp "$REMOTE_CONTROL_SRC" "$REMOTE_CONTROL_TMP" 2>/dev/null || return 1
    chmod 644 "$REMOTE_CONTROL_TMP"
    mv -f "$REMOTE_CONTROL_TMP" "$REMOTE_CONTROL_LIB" 2>/dev/null || {
        rm -f "$REMOTE_CONTROL_TMP"
        return 1
    }

    request_rbp_restart
    say "Remote control prepared: TCP port 7357"
}

remote_control_after_launch()
{
    if [ "$REMOTE_CONTROL_RESIDENT" = "1" ]; then
        say "OK: remote control remains active from the previous insertion"
    elif [ -s "$REMOTE_CONTROL_READY" ]; then
        say "OK: remote control active on TCP port 7357"
    else
        say "WARNING: rbp is active but remote control is inactive"
    fi
    [ -r "$REMOTE_CONTROL_LOG" ] && cat "$REMOTE_CONTROL_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook remote_control_prepare
register_after_launch_hook remote_control_after_launch
