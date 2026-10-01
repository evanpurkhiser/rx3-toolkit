#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Link Export activation. The code lives in the performance core's shared
# object; this module only switches it on.

module_begin link-export-activate link_export_activate

LINK_EXPORT_ACTIVATE_READY=0

link_export_activate_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "Link Export activation disabled: the performance core is not selected"
        return 0
    }
    module_disabled_by_switch link-export-activate && return 0
    LINK_EXPORT_ACTIVATE_READY=1
    module_export RX3_LINK_EXPORT_ACTIVATE 1 "Link Export activation"
    say "Link Export activation prepared on the player's active network"
}

link_export_activate_after_launch()
{
    [ "$LINK_EXPORT_ACTIVATE_READY" = "1" ] || return 0
    say "Link Export activation: the stock network announcement is armed"
}

register_prepare_hook link_export_activate_prepare
register_after_launch_hook link_export_activate_after_launch
