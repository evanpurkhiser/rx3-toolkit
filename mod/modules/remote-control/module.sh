#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Enables the bidirectional panel-control service in the shared runtime.

module_begin remote-control remote_control

REMOTE_CONTROL_READY=/tmp/rx3-remote-control.ready
REMOTE_CONTROL_LOG=/tmp/rx3-remote-control.log

register_ready_file "$REMOTE_CONTROL_READY"
register_diagnostic_file "$REMOTE_CONTROL_LOG"

remote_control_prepare()
{
    [ -r "$CORE_OBJECT" ] || return 1
    rm -f "$REMOTE_CONTROL_READY" "$REMOTE_CONTROL_LOG"
    module_export RX3_REMOTE_CONTROL 1 "Remote control"
}

remote_control_after_launch()
{
    if [ -s "$REMOTE_CONTROL_READY" ]; then
        say "OK: remote control active on TCP port 7357"
    else
        say "WARNING: rbp is active but remote control is inactive"
    fi
    [ -r "$REMOTE_CONTROL_LOG" ] && cat "$REMOTE_CONTROL_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook remote_control_prepare
register_after_launch_hook remote_control_after_launch
