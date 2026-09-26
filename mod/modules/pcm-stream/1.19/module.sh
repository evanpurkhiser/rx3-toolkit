#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Starts a live master PCM stream on any configured IPv4 interface.

module_begin pcm-stream pcm_stream

PCM_STREAM_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
PCM_STREAM_SRC=/mnt/iso/modules/pcm-stream/librx3_pcm_stream.so
PCM_STREAM_LIB=/root/pdj/librx3_pcm_stream.so
PCM_STREAM_TMP=/root/pdj/.librx3_pcm_stream.so.$$
PCM_STREAM_READY=/tmp/rx3-pcm-stream.ready
PCM_STREAM_LOG=/tmp/rx3-pcm-stream.log
PCM_STREAM_STATUS=/tmp/rx3-pcm-stream.status
PCM_STREAM_RESIDENT=0

: "${RX3_PCM_PORT:=7355}"
: "${RX3_PCM_BIND:=0.0.0.0}"
export RX3_PCM_PORT RX3_PCM_BIND

register_ready_file "$PCM_STREAM_READY"
register_diagnostic_file "$PCM_STREAM_LOG"
register_runtime_preload "$PCM_STREAM_LIB"

pcm_stream_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *)   entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$PCM_STREAM_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$PCM_STREAM_LIB:$cleaned"
    else
        RBP_PRELOAD=$PCM_STREAM_LIB
    fi
}

pcm_stream_prepare()
{
    [ "$ACCEPTED" = "$PCM_STREAM_VERIFIED_SHA1" ] || {
        say "PCM stream disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$PCM_STREAM_SRC" ] || {
        say "PCM stream disabled: shared object is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    pcm_stream_normalize_preload
    running_bind=$(rbp_environment_value RX3_PCM_BIND)
    running_port=$(rbp_environment_value RX3_PCM_PORT)
    if preload_contains "$PCM_STREAM_LIB" &&
       cmp -s "$PCM_STREAM_SRC" "$PCM_STREAM_LIB" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$running_bind" = "$RX3_PCM_BIND" ] &&
       [ "$running_port" = "$RX3_PCM_PORT" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        PCM_STREAM_RESIDENT=1
        say "PCM stream already active, rbp left untouched"
        return 0
    fi

    rm -f "$PCM_STREAM_READY" "$PCM_STREAM_LOG" "$PCM_STREAM_STATUS" \
        "$PCM_STREAM_TMP"
    cp "$PCM_STREAM_SRC" "$PCM_STREAM_TMP" 2>/dev/null || return 1
    chmod 644 "$PCM_STREAM_TMP"
    mv -f "$PCM_STREAM_TMP" "$PCM_STREAM_LIB" 2>/dev/null || {
        rm -f "$PCM_STREAM_TMP"
        return 1
    }

    request_rbp_restart
    say "PCM stream prepared: $RX3_PCM_BIND:$RX3_PCM_PORT, signed 16-bit stereo"
}

pcm_stream_after_launch()
{
    if [ "$PCM_STREAM_RESIDENT" = "1" ]; then
        say "OK: PCM stream remains active from the previous insertion"
    elif [ -s "$PCM_STREAM_READY" ]; then
        say "OK: PCM stream active on TCP port $RX3_PCM_PORT"
    else
        say "WARNING: rbp is active but the PCM stream is inactive"
    fi
    [ -r "$PCM_STREAM_LOG" ] && cat "$PCM_STREAM_LOG" >> "$LOG" 2>/dev/null
}

pcm_stream_report()
{
    if [ -r "$PCM_STREAM_STATUS" ]; then
        say "PCM stream status:"
        cat "$PCM_STREAM_STATUS" >> "$LOG" 2>/dev/null
    else
        say "PCM stream status unavailable"
    fi
}

register_prepare_hook pcm_stream_prepare
register_after_launch_hook pcm_stream_after_launch
register_report_hook pcm_stream_report
