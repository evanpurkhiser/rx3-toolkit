#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Registers a volatile, file-backed replacement for one crossfader table.

module_begin crossfader-curve crossfader_curve

CROSSFADER_CURVE_USB_CONFIG=$USB/rx3-crossfader.json
CROSSFADER_CURVE_MODULE_ROOT=${CROSSFADER_CURVE_MODULE_ROOT:-/mnt/iso/modules/crossfader-curve}
CROSSFADER_CURVE_VALIDATOR=$CROSSFADER_CURVE_MODULE_ROOT/validate-config.awk
CROSSFADER_CURVE_GENERATOR=$CROSSFADER_CURVE_MODULE_ROOT/generate-table.awk
CROSSFADER_CURVE_NORMALIZED=/tmp/rx3-crossfader.config.$$
CROSSFADER_CURVE_ESCAPED=/tmp/rx3-crossfader.table.$$.escaped
CROSSFADER_CURVE_PATCHED=/tmp/rx3-crossfader.table.$$.bin
CROSSFADER_CURVE_CONFIGURED=0
CROSSFADER_CURVE_TARGET=""

crossfader_curve_cleanup()
{
    rm -f "$CROSSFADER_CURVE_NORMALIZED" "$CROSSFADER_CURVE_ESCAPED" \
        "$CROSSFADER_CURVE_PATCHED"
}

crossfader_curve_materialize()
{
    : > "$CROSSFADER_CURVE_PATCHED" || return 1
    while IFS= read -r encoded; do
        printf "$encoded" >> "$CROSSFADER_CURVE_PATCHED" || return 1
    done < "$CROSSFADER_CURVE_ESCAPED"
    [ "$(wc -c < "$CROSSFADER_CURVE_PATCHED" 2>/dev/null)" = "2048" ]
}

crossfader_curve_register()
{
    [ -e "$CROSSFADER_CURVE_USB_CONFIG" ] || return 0
    [ -f "$CROSSFADER_CURVE_USB_CONFIG" ] &&
        [ "$(wc -c < "$CROSSFADER_CURVE_USB_CONFIG" 2>/dev/null)" -le 4096 ] &&
        [ -r "$CROSSFADER_CURVE_VALIDATOR" ] &&
        [ -r "$CROSSFADER_CURVE_GENERATOR" ] || {
        say "Crossfader curve rejected: configuration or generator is unreadable"
        return 1
    }

    awk -f "$CROSSFADER_CURVE_VALIDATOR" "$CROSSFADER_CURVE_USB_CONFIG" \
        > "$CROSSFADER_CURVE_NORMALIZED" 2>/dev/null || {
        say "Crossfader curve rejected: invalid rx3-crossfader.json"
        crossfader_curve_cleanup
        return 1
    }
    read -r CROSSFADER_CURVE_TARGET point_count \
        < "$CROSSFADER_CURVE_NORMALIZED" || return 1

    case "$CROSSFADER_CURVE_TARGET" in
        mid)
            offset=4299424
            stock=$CROSSFADER_CURVE_MODULE_ROOT/stock/mid.table
            ;;
        sharp)
            offset=4301472
            stock=$CROSSFADER_CURVE_MODULE_ROOT/stock/sharp.table
            ;;
        mid-half)
            offset=4303520
            stock=$CROSSFADER_CURVE_MODULE_ROOT/stock/mid-half.table
            ;;
        *)
            say "Crossfader curve rejected: normalized target is unknown"
            crossfader_curve_cleanup
            return 1
            ;;
    esac

    awk -f "$CROSSFADER_CURVE_GENERATOR" "$CROSSFADER_CURVE_NORMALIZED" \
        > "$CROSSFADER_CURVE_ESCAPED" 2>/dev/null &&
        crossfader_curve_materialize || {
        say "Crossfader curve rejected: table generation failed"
        crossfader_curve_cleanup
        return 1
    }

    register_patch_file "$offset" 2048 "$stock" "$CROSSFADER_CURVE_PATCHED" \
        "crossfader-$CROSSFADER_CURVE_TARGET" || {
        crossfader_curve_cleanup
        return 1
    }
    CROSSFADER_CURVE_CONFIGURED=1
    say "Crossfader curve registered: target=$CROSSFADER_CURVE_TARGET points=$point_count"
}

crossfader_curve_report()
{
    if [ "$CROSSFADER_CURVE_CONFIGURED" = "1" ]; then
        say "Crossfader curve active: $CROSSFADER_CURVE_TARGET"
    else
        say "Crossfader curve disabled: $CROSSFADER_CURVE_USB_CONFIG is absent"
    fi
    crossfader_curve_cleanup
}

crossfader_curve_register || MODULE_LOAD_FAILED=1
register_report_hook crossfader_curve_report
