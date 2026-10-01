#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Enables the master PCM stream compiled into the shared performance core.

module_begin pcm-stream pcm_stream

PCM_STREAM_READY=/tmp/rx3-pcm-stream.ready
PCM_STREAM_STATUS=/tmp/rx3-pcm-stream.status

: "${RX3_PCM_PORT:=7355}"
: "${RX3_PCM_BIND:=0.0.0.0}"

register_ready_file "$PCM_STREAM_READY"
register_diagnostic_file "$PCM_STREAM_STATUS"

pcm_stream_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "PCM stream disabled: the performance core is not selected"
        return 0
    }
    module_disabled_by_switch pcm-stream && return 0

    rm -f "$PCM_STREAM_READY" "$PCM_STREAM_STATUS" \
        "$PCM_STREAM_STATUS.tmp"
    module_export RX3_PCM_STREAM 1 "PCM stream"
    module_export RX3_PCM_PORT "$RX3_PCM_PORT" "PCM stream"
    module_export RX3_PCM_BIND "$RX3_PCM_BIND" "PCM stream"
    say "PCM stream prepared: $RX3_PCM_BIND:$RX3_PCM_PORT, signed 16-bit stereo"
}

pcm_stream_after_launch()
{
    [ -s "$PCM_STREAM_READY" ] && {
        say "OK: PCM stream active on TCP port $RX3_PCM_PORT"
        return 0
    }
    say "WARNING: rbp is active but the PCM stream is inactive"
}

register_prepare_hook pcm_stream_prepare
register_after_launch_hook pcm_stream_after_launch
