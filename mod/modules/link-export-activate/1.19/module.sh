#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Forces the stock Link Export announcement after rbp starts.

module_begin link-export-activate link_export_activate

LINK_EXPORT_ACTIVATE_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
LINK_EXPORT_ACTIVATE_SOURCE=/mnt/iso/modules/link-export-activate/librx3_link_export_activate.so
LINK_EXPORT_ACTIVATE_LIBRARY=/root/pdj/librx3_link_export_activate.so
LINK_EXPORT_ACTIVATE_TEMP=/root/pdj/.librx3_link_export_activate.so.$$
LINK_EXPORT_ACTIVATE_LOG=/tmp/rx3-link-export-activate.log

register_runtime_preload "$LINK_EXPORT_ACTIVATE_LIBRARY"
register_diagnostic_file "$LINK_EXPORT_ACTIVATE_LOG"

link_export_activate_normalize_preload()
{
    pending=$RBP_PRELOAD
    cleaned=
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *) entry=$pending; pending= ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$LINK_EXPORT_ACTIVATE_LIBRARY" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned=$cleaned:$entry
        else
            cleaned=$entry
        fi
    done

    if [ -n "$cleaned" ]; then
        RBP_PRELOAD=$LINK_EXPORT_ACTIVATE_LIBRARY:$cleaned
    else
        RBP_PRELOAD=$LINK_EXPORT_ACTIVATE_LIBRARY
    fi
}

link_export_activate_prepare()
{
    [ "$ACCEPTED" = "$LINK_EXPORT_ACTIVATE_VERIFIED_SHA1" ] || {
        say "Link Export activation disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$LINK_EXPORT_ACTIVATE_SOURCE" ] || {
        say "Link Export activation disabled: preload is missing"
        return 1
    }

    previous_preload=$RBP_PRELOAD
    link_export_activate_normalize_preload
    if preload_contains "$LINK_EXPORT_ACTIVATE_LIBRARY" &&
       cmp -s "$LINK_EXPORT_ACTIVATE_SOURCE" "$LINK_EXPORT_ACTIVATE_LIBRARY" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ]; then
        say "Link Export activation already installed"
        return 0
    fi

    cp "$LINK_EXPORT_ACTIVATE_SOURCE" "$LINK_EXPORT_ACTIVATE_TEMP" || return 1
    chmod 644 "$LINK_EXPORT_ACTIVATE_TEMP" || return 1
    mv -f "$LINK_EXPORT_ACTIVATE_TEMP" "$LINK_EXPORT_ACTIVATE_LIBRARY" || {
        rm -f "$LINK_EXPORT_ACTIVATE_TEMP"
        return 1
    }
    rm -f "$LINK_EXPORT_ACTIVATE_LOG"
    request_rbp_restart
    say "Link Export announcement armed on the active network"
}

link_export_activate_report()
{
    [ -r "$LINK_EXPORT_ACTIVATE_LOG" ] && cat "$LINK_EXPORT_ACTIVATE_LOG" >> "$LOG" 2>&1
}

register_prepare_hook link_export_activate_prepare
register_report_hook link_export_activate_report
