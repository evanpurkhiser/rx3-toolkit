#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Installs a volatile, exact-build-gated crossfader table preload.

module_begin crossfader-curve crossfader_curve

CROSSFADER_CURVE_VERIFIED_SHA1=cf309238491e73cdbdc1f08a09f7a3177e079068
CROSSFADER_CURVE_USB_CONFIG=$USB/rx3-crossfader.json
CROSSFADER_CURVE_CONFIG=/root/pdj/rx3-crossfader.config
CROSSFADER_CURVE_NORMALIZED=/tmp/rx3-crossfader.config.$$
CROSSFADER_CURVE_SRC=/mnt/iso/modules/crossfader-curve/librx3_crossfader_curve.so
CROSSFADER_CURVE_LIB=/root/pdj/librx3_crossfader_curve.so
CROSSFADER_CURVE_VALIDATOR=/mnt/iso/modules/crossfader-curve/validate-config.awk
CROSSFADER_CURVE_READY=/tmp/rx3-crossfader-curve.ready
CROSSFADER_CURVE_LOG=/tmp/rx3-crossfader-curve.log
CROSSFADER_CURVE_RESIDENT=0
CROSSFADER_CURVE_CONFIGURED=0

register_runtime_preload "$CROSSFADER_CURVE_LIB"

crossfader_curve_without_preload()
{
    pending=$1
    cleaned=""
    while [ -n "$pending" ]; do
        case "$pending" in
            *:*) entry=${pending%%:*}; pending=${pending#*:} ;;
            *) entry=$pending; pending="" ;;
        esac
        [ -n "$entry" ] || continue
        [ "$entry" = "$CROSSFADER_CURVE_LIB" ] && continue
        if [ -n "$cleaned" ]; then
            cleaned="$cleaned:$entry"
        else
            cleaned=$entry
        fi
    done
    printf '%s' "$cleaned"
}

crossfader_curve_with_preload()
{
    cleaned=$(crossfader_curve_without_preload "$RBP_PRELOAD")
    if [ -n "$cleaned" ]; then
        RBP_PRELOAD="$CROSSFADER_CURVE_LIB:$cleaned"
    else
        RBP_PRELOAD=$CROSSFADER_CURVE_LIB
    fi
}

crossfader_curve_install()
{
    source_file=$1
    target_file=$2
    temporary_file=$target_file.$$
    cp "$source_file" "$temporary_file" 2>/dev/null || return 1
    chmod 644 "$temporary_file"
    mv -f "$temporary_file" "$target_file" 2>/dev/null || {
        rm -f "$temporary_file"
        return 1
    }
}

crossfader_curve_prepare()
{
    if [ ! -e "$CROSSFADER_CURVE_USB_CONFIG" ]; then
        cleaned=$(crossfader_curve_without_preload "$RBP_PRELOAD")
        if [ "$cleaned" != "$RBP_PRELOAD" ]; then
            RBP_PRELOAD=$cleaned
            request_rbp_restart
            say "Crossfader curve disabled: configuration removed"
        else
            say "Crossfader curve disabled: $CROSSFADER_CURVE_USB_CONFIG is absent"
        fi
        return 0
    fi

    [ "$ACCEPTED" = "$CROSSFADER_CURVE_VERIFIED_SHA1" ] || {
        say "Crossfader curve disabled: rbp build $ACCEPTED has not been verified"
        return 1
    }
    [ -r "$CROSSFADER_CURVE_SRC" ] && [ -r "$CROSSFADER_CURVE_VALIDATOR" ] || {
        say "Crossfader curve disabled: module files are missing"
        return 1
    }

    awk -f "$CROSSFADER_CURVE_VALIDATOR" "$CROSSFADER_CURVE_USB_CONFIG" \
        > "$CROSSFADER_CURVE_NORMALIZED" 2>/dev/null || {
        rm -f "$CROSSFADER_CURVE_NORMALIZED"
        say "Crossfader curve disabled: invalid rx3-crossfader.json"
        return 1
    }
    read -r curve_target point_count < "$CROSSFADER_CURVE_NORMALIZED" || return 1
    say "Crossfader curve config: target=$curve_target points=$point_count"
    CROSSFADER_CURVE_CONFIGURED=1
    register_ready_file "$CROSSFADER_CURVE_READY"
    register_diagnostic_file "$CROSSFADER_CURVE_LOG"

    previous_preload=$RBP_PRELOAD
    crossfader_curve_with_preload
    if preload_contains "$CROSSFADER_CURVE_LIB" &&
       cmp -s "$CROSSFADER_CURVE_SRC" "$CROSSFADER_CURVE_LIB" &&
       cmp -s "$CROSSFADER_CURVE_NORMALIZED" "$CROSSFADER_CURVE_CONFIG" &&
       [ "$RBP_PRELOAD" = "$previous_preload" ] &&
       [ "$NEED_RBP_RESTART" = "0" ]; then
        CROSSFADER_CURVE_RESIDENT=1
        rm -f "$CROSSFADER_CURVE_NORMALIZED"
        say "Crossfader curve already active, rbp left untouched"
        return 0
    fi

    rm -f "$CROSSFADER_CURVE_READY" "$CROSSFADER_CURVE_LOG"
    crossfader_curve_install "$CROSSFADER_CURVE_SRC" "$CROSSFADER_CURVE_LIB" &&
    crossfader_curve_install "$CROSSFADER_CURVE_NORMALIZED" "$CROSSFADER_CURVE_CONFIG" || {
        rm -f "$CROSSFADER_CURVE_NORMALIZED"
        say "Crossfader curve disabled: files could not be installed"
        return 1
    }
    rm -f "$CROSSFADER_CURVE_NORMALIZED"

    request_rbp_restart
    say "Crossfader curve prepared: volatile in-memory table replacement"
}

crossfader_curve_after_launch()
{
    [ "$CROSSFADER_CURVE_CONFIGURED" = "1" ] || return 0
    if [ "$CROSSFADER_CURVE_RESIDENT" = "1" ]; then
        say "OK: crossfader curve remains active from the previous insertion"
    elif [ -s "$CROSSFADER_CURVE_READY" ]; then
        say "OK: crossfader curve active"
    else
        say "WARNING: rbp is active but the crossfader curve is inactive"
    fi
    [ -r "$CROSSFADER_CURVE_LOG" ] && cat "$CROSSFADER_CURVE_LOG" >> "$LOG" 2>/dev/null
}

register_prepare_hook crossfader_curve_prepare
register_after_launch_hook crossfader_curve_after_launch
