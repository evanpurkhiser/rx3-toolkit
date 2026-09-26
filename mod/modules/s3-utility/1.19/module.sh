#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# ESP32-S3 utility integration.
# The code lives in the UI core's shared object; this module decides
# whether it runs, and owns its documentation and tests.

module_begin s3-utility s3_utility

S3_UTILITY_READY=0

s3_utility_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "ESP32-S3 utility integration disabled: the RX3 UI core is not selected"
        return 1
    }
    export RX3_S3_UTILITY=1
    S3_UTILITY_READY=1
    running=$(rbp_environment_value RX3_S3_UTILITY)
    if [ "$running" != "1" ]; then
        say "ESP32-S3 utility integration needs a restart: running rbp carries RX3_S3_UTILITY=[${running:-none}]"
        request_rbp_restart
    fi
    say "ESP32-S3 utility integration prepared"
}

s3_utility_after_launch()
{
    [ "$S3_UTILITY_READY" = "1" ] || return 0
    say "ESP32-S3 Utility section active"
}

register_prepare_hook s3_utility_prepare
register_after_launch_hook s3_utility_after_launch
