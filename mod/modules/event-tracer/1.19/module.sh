#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Installs a RAM-only JSON event tracer for one verified RX3 1.19 rbp build.

module_begin event-tracer event_tracer

EVENT_TRACER_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
EVENT_TRACER_SRC=/mnt/iso/modules/event-tracer/librx3_event_tracer.so
EVENT_TRACER_LIB=/root/pdj/librx3_event_tracer.so
EVENT_TRACER_TMP=/root/pdj/.librx3_event_tracer.so.$$
EVENT_TRACER_READY=/tmp/rx3-event-tracer.ready
EVENT_TRACER_LOG=/tmp/rx3-event-tracer.log
EVENT_TRACER_RESIDENT=0

register_ready_file "$EVENT_TRACER_READY"
register_diagnostic_file "$EVENT_TRACER_LOG"
register_runtime_preload "$EVENT_TRACER_LIB"

event_tracer_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *)   entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$EVENT_TRACER_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$EVENT_TRACER_LIB:$cleaned"
    else
        RBP_PRELOAD=$EVENT_TRACER_LIB
    fi
}

event_tracer_prepare()
{
    [ "$ACCEPTED" = "$EVENT_TRACER_VERIFIED_SHA1" ] || {
        say "Event tracer disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$EVENT_TRACER_SRC" ] || {
        say "Event tracer disabled: shared object is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    event_tracer_normalize_preload
    if preload_contains "$EVENT_TRACER_LIB" &&
       cmp -s "$EVENT_TRACER_SRC" "$EVENT_TRACER_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        EVENT_TRACER_RESIDENT=1
        say "Event tracer already active, rbp left untouched"
        return 0
    fi

    rm -f "$EVENT_TRACER_READY" "$EVENT_TRACER_LOG" "$EVENT_TRACER_TMP"
    cp "$EVENT_TRACER_SRC" "$EVENT_TRACER_TMP" 2>/dev/null || return 1
    chmod 644 "$EVENT_TRACER_TMP"
    mv -f "$EVENT_TRACER_TMP" "$EVENT_TRACER_LIB" 2>/dev/null || {
        rm -f "$EVENT_TRACER_TMP"
        return 1
    }

    request_rbp_restart
    say "Event tracer prepared: JSONL output in /dev/shm/rx3-events.jsonl"
}

event_tracer_after_launch()
{
    if [ "$EVENT_TRACER_RESIDENT" = "1" ]; then
        say "OK: event tracer remains active from the previous insertion"
    elif [ -s "$EVENT_TRACER_READY" ]; then
        say "OK: event tracer active"
    else
        say "WARNING: rbp is active but the event tracer is inactive"
    fi
    [ -r "$EVENT_TRACER_LOG" ] && cat "$EVENT_TRACER_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook event_tracer_prepare
register_after_launch_hook event_tracer_after_launch
