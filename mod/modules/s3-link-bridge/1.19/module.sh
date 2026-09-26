#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# CDC-NCM bring-up for the ESP32-S3 Link Export bridge.

module_begin s3-link-bridge s3_link_bridge

: "${S3_LINK_BRIDGE_USB_SYSFS:=/sys/bus/usb/devices}"
: "${S3_LINK_BRIDGE_NET_SYSFS:=/sys/class/net}"
: "${S3_LINK_BRIDGE_PROC_MODULES:=/proc/modules}"
: "${S3_LINK_BRIDGE_STATE:=/tmp/rx3-s3-link-bridge.state}"
: "${S3_LINK_BRIDGE_MANAGEMENT_ADDRESS:=172.31.254.2}"
: "${S3_LINK_BRIDGE_MANAGEMENT_NETMASK:=255.255.255.252}"

S3_LINK_BRIDGE_USB_VENDOR=303a
S3_LINK_BRIDGE_USB_PRODUCT=4012
S3_LINK_BRIDGE_USB_NAME='RX3 Wi-Fi Link Bridge'
S3_LINK_BRIDGE_MODULES=/mnt/iso/modules/s3-link-bridge
S3_LINK_BRIDGE_APPLICATION_INTERFACE=usb0
S3_LINK_BRIDGE_REAR_INTERFACE=eth0
S3_LINK_BRIDGE_INTERFACE=
S3_LINK_BRIDGE_TOPOLOGY=

# The application networking paths in rbp 1.19 share this string. Its stock
# DHCP command has a separate embedded interface argument, so both sites must
# move together. Keeping the kernel names intact preserves rear USB-B recovery.
register_patch 4155000 '\145\164\150\060' '\165\163\142\060' network-interface-eth0-to-usb0
register_patch 5084504 '\151\040\145\164' '\151\040\165\163' dhcp-interface-prefix-eth0-to-usb0
register_patch 5084508 '\150\060\040\055' '\142\060\040\055' dhcp-interface-suffix-eth0-to-usb0

s3_link_bridge_read_attribute()
{
    tr -d '\r\n' < "$1" 2>/dev/null
}

s3_link_bridge_module_loaded()
{
    awk -v wanted="$1" '$1 == wanted { found = 1 } END { exit !found }' \
        "$S3_LINK_BRIDGE_PROC_MODULES" 2>/dev/null
}

s3_link_bridge_load_modules()
{
    if ! s3_link_bridge_module_loaded usbnet; then
        insmod "$S3_LINK_BRIDGE_MODULES/usbnet.ko" >/dev/null 2>&1 || {
            say "S3 Link bridge disabled: usbnet.ko did not load"
            return 1
        }
    fi

    if ! s3_link_bridge_module_loaded cdc_ncm; then
        insmod "$S3_LINK_BRIDGE_MODULES/cdc_ncm.ko" >/dev/null 2>&1 || {
            say "S3 Link bridge disabled: cdc_ncm.ko did not load after usbnet.ko"
            return 1
        }
    fi
}

s3_link_bridge_detect_interface()
{
    matches=0
    matched_interface=
    matched_topology=

    for usb_device in "$S3_LINK_BRIDGE_USB_SYSFS"/*; do
        [ -f "$usb_device/idVendor" ] || continue
        [ "$(s3_link_bridge_read_attribute "$usb_device/idVendor")" = \
            "$S3_LINK_BRIDGE_USB_VENDOR" ] || continue
        [ "$(s3_link_bridge_read_attribute "$usb_device/idProduct")" = \
            "$S3_LINK_BRIDGE_USB_PRODUCT" ] || continue
        [ "$(s3_link_bridge_read_attribute "$usb_device/product")" = \
            "$S3_LINK_BRIDGE_USB_NAME" ] || continue

        for net_path in "$usb_device"/net/* "$usb_device":*/net/*; do
            [ -e "$net_path" ] || continue
            interface=${net_path##*/}
            [ -e "$S3_LINK_BRIDGE_NET_SYSFS/$interface" ] || continue
            matches=$((matches + 1))
            matched_interface=$interface
            matched_topology=${usb_device##*/}
        done
    done

    [ "$matches" = "1" ] || {
        say "S3 Link bridge disabled: expected one matching NCM interface, found $matches"
        return 1
    }
    [ "$matched_interface" = "$S3_LINK_BRIDGE_APPLICATION_INTERFACE" ] || {
        say "S3 Link bridge disabled: expected $S3_LINK_BRIDGE_APPLICATION_INTERFACE, found $matched_interface"
        return 1
    }

    S3_LINK_BRIDGE_INTERFACE=$matched_interface
    S3_LINK_BRIDGE_TOPOLOGY=$matched_topology
}

s3_link_bridge_carrier_ready()
{
    [ "$(cat "$S3_LINK_BRIDGE_NET_SYSFS/$S3_LINK_BRIDGE_INTERFACE/carrier" 2>/dev/null)" = "1" ]
}

s3_link_bridge_wait_for_carrier()
{
    attempts=0
    while [ "$attempts" -lt 20 ]; do
        s3_link_bridge_carrier_ready && return 0
        sleep 1
        attempts=$((attempts + 1))
    done
    return 1
}

s3_link_bridge_probe()
{
    s3_link_bridge_load_modules || return 1
    s3_link_bridge_detect_interface || return 1
    ifconfig "$S3_LINK_BRIDGE_INTERFACE" up >/dev/null 2>&1 || {
        say "S3 Link bridge disabled: could not raise $S3_LINK_BRIDGE_INTERFACE"
        return 1
    }
    s3_link_bridge_wait_for_carrier || {
        say "S3 Link bridge disabled: S3 did not raise NCM carrier after Wi-Fi association"
        return 1
    }

    say "S3 Link bridge ready for patched rbp on $S3_LINK_BRIDGE_INTERFACE"
    say "Rear USB recovery remains available on $S3_LINK_BRIDGE_REAR_INTERFACE"
}

s3_link_bridge_prepare()
{
    s3_link_bridge_probe || {
        say "S3 Link bridge probe failed; refusing to redirect rbp from rear USB"
        return 1
    }
}

s3_link_bridge_configure_management()
{
    ifconfig "$S3_LINK_BRIDGE_INTERFACE:mgmt" \
        "$S3_LINK_BRIDGE_MANAGEMENT_ADDRESS" \
        netmask "$S3_LINK_BRIDGE_MANAGEMENT_NETMASK" up >/dev/null 2>&1 || {
        say "S3 Link bridge warning: could not configure management alias"
        return 1
    }
}

s3_link_bridge_write_state()
{
    {
        echo "mode=rbp-dhcp+management"
        echo "interface=$S3_LINK_BRIDGE_INTERFACE"
        echo "usb_topology=$S3_LINK_BRIDGE_TOPOLOGY"
        echo "application_interface=$S3_LINK_BRIDGE_APPLICATION_INTERFACE"
        echo "rear_interface=$S3_LINK_BRIDGE_REAR_INTERFACE"
        echo "management_alias=$S3_LINK_BRIDGE_INTERFACE:mgmt"
        echo "management_address=$S3_LINK_BRIDGE_MANAGEMENT_ADDRESS"
        echo "carrier=$(cat "$S3_LINK_BRIDGE_NET_SYSFS/$S3_LINK_BRIDGE_INTERFACE/carrier" 2>/dev/null)"
        ifconfig "$S3_LINK_BRIDGE_INTERFACE" 2>/dev/null
    } > "$S3_LINK_BRIDGE_STATE"
}

s3_link_bridge_after_launch()
{
    s3_link_bridge_configure_management || return 1
    s3_link_bridge_write_state
    say "S3 Link bridge active; rbp owns DHCP on $S3_LINK_BRIDGE_INTERFACE"
}

s3_link_bridge_report()
{
    [ -r "$S3_LINK_BRIDGE_STATE" ] && cat "$S3_LINK_BRIDGE_STATE" >> "$LOG" 2>&1
}

register_prepare_hook s3_link_bridge_prepare
register_after_launch_hook s3_link_bridge_after_launch
register_report_hook s3_link_bridge_report
