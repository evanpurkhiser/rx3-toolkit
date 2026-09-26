#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Drives the stock PC-mounted transition after rbp binds to the S3 usb0.

module_begin s3-link-activation s3_link_activation

S3_LINK_ACTIVATION_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
S3_LINK_ACTIVATION_SOURCE=/mnt/iso/modules/s3-link-activation/librx3_link_bootstrap.so
S3_LINK_ACTIVATION_LIBRARY=/root/pdj/librx3_link_bootstrap.so
S3_LINK_ACTIVATION_TEMP=/root/pdj/.librx3_link_bootstrap.so.$$
S3_LINK_ACTIVATION_LOG=/tmp/rx3-link-bootstrap.log

register_runtime_preload "$S3_LINK_ACTIVATION_LIBRARY"
register_diagnostic_file "$S3_LINK_ACTIVATION_LOG"

s3_link_activation_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *) entry=$pending; pending= ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$S3_LINK_ACTIVATION_LIBRARY" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned=$cleaned:$entry
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD=$S3_LINK_ACTIVATION_LIBRARY:$cleaned
    else
        RBP_PRELOAD=$S3_LINK_ACTIVATION_LIBRARY
    fi
}

s3_link_activation_prepare()
{
    [ "$ACCEPTED" = "$S3_LINK_ACTIVATION_VERIFIED_SHA1" ] || {
        say "S3 Link activation disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$S3_LINK_ACTIVATION_SOURCE" ] || {
        say "S3 Link activation disabled: preload is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    s3_link_activation_normalize_preload
    if preload_contains "$S3_LINK_ACTIVATION_LIBRARY" &&
       cmp -s "$S3_LINK_ACTIVATION_SOURCE" "$S3_LINK_ACTIVATION_LIBRARY" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ]; then
        say "S3 Link activation already installed"
        return 0
    fi

    cp "$S3_LINK_ACTIVATION_SOURCE" "$S3_LINK_ACTIVATION_TEMP" || return 1
    chmod 644 "$S3_LINK_ACTIVATION_TEMP" || return 1
    mv -f "$S3_LINK_ACTIVATION_TEMP" "$S3_LINK_ACTIVATION_LIBRARY" || {
        rm -f "$S3_LINK_ACTIVATION_TEMP"
        return 1
    }
    rm -f "$S3_LINK_ACTIVATION_LOG"
    request_rbp_restart
    say "S3 Link activation armed through the stock PC-mounted callback"
}

s3_link_activation_report()
{
    [ -r "$S3_LINK_ACTIVATION_LOG" ] && cat "$S3_LINK_ACTIVATION_LOG" >> "$LOG" 2>&1
}

register_prepare_hook s3_link_activation_prepare
register_report_hook s3_link_activation_report
