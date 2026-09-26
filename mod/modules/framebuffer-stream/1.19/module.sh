#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Starts a RAM-only framebuffer tile stream on every configured network link.

module_begin framebuffer-stream framebuffer_stream

FRAMEBUFFER_STREAM_SRC=/mnt/iso/modules/framebuffer-stream/librx3_framebuffer_stream.so
FRAMEBUFFER_STREAM_LIB=/root/pdj/librx3_framebuffer_stream.so
FRAMEBUFFER_STREAM_TMP=/root/pdj/.librx3_framebuffer_stream.so.$$
FRAMEBUFFER_STREAM_READY=/tmp/rx3-framebuffer-stream.ready
FRAMEBUFFER_STREAM_LOG=/tmp/rx3-framebuffer-stream.log
FRAMEBUFFER_STREAM_RESIDENT=0

register_ready_file "$FRAMEBUFFER_STREAM_READY"
register_diagnostic_file "$FRAMEBUFFER_STREAM_LOG"
register_runtime_preload "$FRAMEBUFFER_STREAM_LIB"

framebuffer_stream_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *)   entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$FRAMEBUFFER_STREAM_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$FRAMEBUFFER_STREAM_LIB:$cleaned"
    else
        RBP_PRELOAD=$FRAMEBUFFER_STREAM_LIB
    fi
}

framebuffer_stream_prepare()
{
    [ -r "$FRAMEBUFFER_STREAM_SRC" ] || {
        say "Framebuffer stream disabled: shared object is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    framebuffer_stream_normalize_preload
    if preload_contains "$FRAMEBUFFER_STREAM_LIB" &&
       cmp -s "$FRAMEBUFFER_STREAM_SRC" "$FRAMEBUFFER_STREAM_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        FRAMEBUFFER_STREAM_RESIDENT=1
        say "Framebuffer stream already active, rbp left untouched"
        return 0
    fi

    rm -f "$FRAMEBUFFER_STREAM_READY" "$FRAMEBUFFER_STREAM_LOG" \
        "$FRAMEBUFFER_STREAM_TMP"
    cp "$FRAMEBUFFER_STREAM_SRC" "$FRAMEBUFFER_STREAM_TMP" 2>/dev/null || return 1
    chmod 644 "$FRAMEBUFFER_STREAM_TMP"
    mv -f "$FRAMEBUFFER_STREAM_TMP" "$FRAMEBUFFER_STREAM_LIB" 2>/dev/null || {
        rm -f "$FRAMEBUFFER_STREAM_TMP"
        return 1
    }

    request_rbp_restart
    say "Framebuffer stream prepared: TCP port 7351, protocol v1"
}

framebuffer_stream_after_launch()
{
    if [ "$FRAMEBUFFER_STREAM_RESIDENT" = "1" ]; then
        say "OK: framebuffer stream remains active from the previous insertion"
    elif [ -s "$FRAMEBUFFER_STREAM_READY" ]; then
        say "OK: framebuffer stream active on TCP port 7351"
    else
        say "WARNING: rbp is active but the framebuffer stream is inactive"
    fi
    [ -r "$FRAMEBUFFER_STREAM_LOG" ] && \
        cat "$FRAMEBUFFER_STREAM_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook framebuffer_stream_prepare
register_after_launch_hook framebuffer_stream_after_launch
