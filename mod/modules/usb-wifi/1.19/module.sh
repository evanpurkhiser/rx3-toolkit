#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Native Wi-Fi bring-up for the Edimax 7392:7811 USB adapter.

module_begin usb-wifi usb_wifi

: "${USB_WIFI_USB_SYSFS:=/sys/bus/usb/devices}"
: "${USB_WIFI_USB_DRIVER:=/sys/bus/usb/drivers/usb}"
: "${USB_WIFI_NET_SYSFS:=/sys/class/net}"
: "${USB_WIFI_PROC_MODULES:=/proc/modules}"
: "${USB_WIFI_CONFIG:=$USB/RX3_WIFI/wpa_supplicant.conf}"
: "${USB_WIFI_STATE:=/tmp/rx3-usb-wifi.state}"

USB_WIFI_VENDOR=7392
USB_WIFI_PRODUCT=7811
USB_WIFI_DIRECTORY=/mnt/iso/modules/usb-wifi
USB_WIFI_FIRMWARE_DIRECTORY=/lib/firmware/rtlwifi
: "${USB_WIFI_RUNTIME_DIRECTORY:=/dev/shm/rx3-usb-wifi}"
USB_WIFI_SOURCE_INTERFACE=
USB_WIFI_INTERFACE=
USB_WIFI_APPLICATION_INTERFACE=eth0
USB_WIFI_REAR_INTERFACE=usbB0
USB_WIFI_TOPOLOGY=
USB_WIFI_PID_FILE=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.pid
USB_WIFI_LOG=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.log
USB_WIFI_RUNTIME_CONFIG=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.conf
USB_WIFI_SUPPLICANT=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant
USB_WIFI_CLI=$USB_WIFI_RUNTIME_DIRECTORY/wpa_cli
USB_WIFI_IFRENAME=$USB_WIFI_RUNTIME_DIRECTORY/rx3-ifrename
USB_WIFI_DHCP_ATTEMPTS=25
USB_WIFI_DHCP_RETRY_ATTEMPTS=10
USB_WIFI_ADDRESS=
USB_WIFI_DHCP_STATE=pending
USB_WIFI_SWAP_CHANGED=0

register_diagnostic_file "$USB_WIFI_LOG"

usb_wifi_read_attribute()
{
    tr -d '\r\n' < "$1" 2>/dev/null
}

usb_wifi_module_loaded()
{
    awk -v wanted="$1" '$1 == wanted { found = 1 } END { exit !found }' \
        "$USB_WIFI_PROC_MODULES" 2>/dev/null
}

usb_wifi_install_firmware()
{
    mkdir -p "$USB_WIFI_FIRMWARE_DIRECTORY" || return 1
    cp "$USB_WIFI_DIRECTORY/rtlwifi/rtl8192cufw.bin" \
        "$USB_WIFI_FIRMWARE_DIRECTORY/rtl8192cufw.bin" || return 1
    chmod 644 "$USB_WIFI_FIRMWARE_DIRECTORY/rtl8192cufw.bin"
}

usb_wifi_load_one()
{
    module_name=$1
    module_file=$2
    usb_wifi_module_loaded "$module_name" && return 0
    insmod "$USB_WIFI_DIRECTORY/$module_file" >/dev/null 2>&1 || {
        say "USB Wi-Fi disabled: $module_file did not load"
        return 1
    }
}

usb_wifi_load_modules()
{
    usb_wifi_load_one compat_average compat-average.ko || return 1
    usb_wifi_load_one cfg80211 cfg80211.ko || return 1
    usb_wifi_load_one mac80211 mac80211.ko || return 1
    usb_wifi_load_one rtlwifi rtlwifi.ko || return 1
    usb_wifi_load_one rtl8192c_common rtl8192c-common.ko || return 1
    usb_wifi_load_one rtl8192cu rtl8192cu.ko || return 1
}

usb_wifi_detect_interface()
{
    matches=0
    matched_interface=
    matched_topology=

    for usb_device in "$USB_WIFI_USB_SYSFS"/*; do
        [ -f "$usb_device/idVendor" ] || continue
        [ "$(usb_wifi_read_attribute "$usb_device/idVendor")" = \
            "$USB_WIFI_VENDOR" ] || continue
        [ "$(usb_wifi_read_attribute "$usb_device/idProduct")" = \
            "$USB_WIFI_PRODUCT" ] || continue

        for net_path in "$usb_device"/net/* "$usb_device":*/net/*; do
            [ -e "$net_path" ] || continue
            interface=${net_path##*/}
            [ -e "$USB_WIFI_NET_SYSFS/$interface" ] || continue
            matches=$((matches + 1))
            matched_interface=$interface
            matched_topology=${usb_device##*/}
        done
    done

    [ "$matches" = "1" ] || {
        say "USB Wi-Fi disabled: expected one 7392:7811 interface, found $matches"
        return 1
    }

    USB_WIFI_SOURCE_INTERFACE=$matched_interface
    USB_WIFI_INTERFACE=$matched_interface
    USB_WIFI_TOPOLOGY=$matched_topology
}

usb_wifi_stage_runtime()
{
    [ -r "$USB_WIFI_CONFIG" ] || {
        say "USB Wi-Fi disabled: missing $USB_WIFI_CONFIG"
        return 1
    }
    mkdir -p "$USB_WIFI_RUNTIME_DIRECTORY" || return 1
    cp "$USB_WIFI_DIRECTORY/wpa_supplicant" "$USB_WIFI_SUPPLICANT" || return 1
    cp "$USB_WIFI_DIRECTORY/wpa_cli" "$USB_WIFI_CLI" || return 1
    cp "$USB_WIFI_DIRECTORY/rx3-ifrename" "$USB_WIFI_IFRENAME" || return 1
    cp "$USB_WIFI_CONFIG" "$USB_WIFI_RUNTIME_CONFIG" || return 1
    chmod 700 "$USB_WIFI_SUPPLICANT" "$USB_WIFI_CLI" "$USB_WIFI_IFRENAME" || return 1
    chmod 600 "$USB_WIFI_RUNTIME_CONFIG" || return 1
}

usb_wifi_stop_owned_supplicant()
{
    [ -r "$USB_WIFI_PID_FILE" ] || return 0
    old_pid=$(cat "$USB_WIFI_PID_FILE" 2>/dev/null)
    case "$old_pid" in ""|*[!0-9]*) return 0 ;; esac
    [ -r "/proc/$old_pid/cmdline" ] || return 0
    tr '\0' ' ' < "/proc/$old_pid/cmdline" 2>/dev/null | \
        grep -Fq "$USB_WIFI_SUPPLICANT" || return 0
    kill "$old_pid" >/dev/null 2>&1 || return 1
    attempts=0
    while [ "$attempts" -lt 5 ] && [ -d "/proc/$old_pid" ]; do
        sleep 1
        attempts=$((attempts + 1))
    done
    if [ -d "/proc/$old_pid" ]; then
        kill -9 "$old_pid" >/dev/null 2>&1 || return 1
        sleep 1
    fi
    rm -f "$USB_WIFI_PID_FILE"
}

usb_wifi_start_supplicant()
{
    usb_wifi_stop_owned_supplicant || return 1
    rm -f "$USB_WIFI_PID_FILE" "$USB_WIFI_LOG"
    rm -f "$USB_WIFI_RUNTIME_DIRECTORY/control/$USB_WIFI_INTERFACE"
    ifconfig "$USB_WIFI_INTERFACE" up >/dev/null 2>&1 || return 1
    "$USB_WIFI_SUPPLICANT" -Dnl80211 -i"$USB_WIFI_INTERFACE" \
        -c"$USB_WIFI_RUNTIME_CONFIG" >"$USB_WIFI_LOG" 2>&1 &
    supplicant_pid=$!
    echo "$supplicant_pid" > "$USB_WIFI_PID_FILE"
    sleep 1
    kill -0 "$supplicant_pid" 2>/dev/null || {
        say "USB Wi-Fi disabled: wpa_supplicant did not start"
        return 1
    }
}

usb_wifi_wait_for_association()
{
    attempts=0
    while [ "$attempts" -lt 30 ]; do
        [ "$(cat "$USB_WIFI_NET_SYSFS/$USB_WIFI_INTERFACE/carrier" 2>/dev/null)" = "1" ] && \
            return 0
        sleep 1
        attempts=$((attempts + 1))
    done
    say "USB Wi-Fi disabled: association timed out"
    return 1
}

usb_wifi_restore_original_links()
{
    ifconfig "$USB_WIFI_SOURCE_INTERFACE" up >/dev/null 2>&1 || true
    ifconfig "$USB_WIFI_APPLICATION_INTERFACE" up >/dev/null 2>&1 || true
}

usb_wifi_unbind_adapter()
{
    case "$USB_WIFI_TOPOLOGY" in
        ""|*[!0-9.-]*) return 1 ;;
    esac
    [ -w "$USB_WIFI_USB_DRIVER/unbind" ] || return 1
    printf '%s' "$USB_WIFI_TOPOLOGY" > "$USB_WIFI_USB_DRIVER/unbind"
}

usb_wifi_release_application_name()
{
    attempts=0
    while [ "$attempts" -lt 3 ]; do
        "$USB_WIFI_IFRENAME" "$USB_WIFI_APPLICATION_INTERFACE" \
            "$USB_WIFI_SOURCE_INTERFACE" >/dev/null 2>&1 && return 0
        attempts=$((attempts + 1))
        sleep 1
    done

    say "USB Wi-Fi rollback: unbinding the adapter to reclaim $USB_WIFI_APPLICATION_INTERFACE"
    usb_wifi_unbind_adapter || return 1
    attempts=0
    while [ "$attempts" -lt 3 ]; do
        [ ! -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_APPLICATION_INTERFACE" ] && return 0
        attempts=$((attempts + 1))
        sleep 1
    done
    return 1
}

usb_wifi_rollback_swap()
{
    [ "$USB_WIFI_SWAP_CHANGED" = "1" ] || return 0
    usb_wifi_stop_owned_supplicant >/dev/null 2>&1 || true
    [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || {
        usb_wifi_restore_original_links
        return 0
    }

    ifconfig "$USB_WIFI_APPLICATION_INTERFACE" down >/dev/null 2>&1 || true
    ifconfig "$USB_WIFI_REAR_INTERFACE" down >/dev/null 2>&1 || true
    if [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_APPLICATION_INTERFACE" ] && \
        [ "$USB_WIFI_SOURCE_INTERFACE" != "$USB_WIFI_APPLICATION_INTERFACE" ]; then
        usb_wifi_release_application_name || return 1
    fi
    [ ! -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_APPLICATION_INTERFACE" ] || return 1
    "$USB_WIFI_IFRENAME" "$USB_WIFI_REAR_INTERFACE" \
        "$USB_WIFI_APPLICATION_INTERFACE" >/dev/null 2>&1 || return 1
    USB_WIFI_INTERFACE=$USB_WIFI_SOURCE_INTERFACE
    USB_WIFI_SWAP_CHANGED=0
    usb_wifi_restore_original_links
}

usb_wifi_swap_interfaces()
{
    if [ "$USB_WIFI_SOURCE_INTERFACE" = "$USB_WIFI_APPLICATION_INTERFACE" ]; then
        [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || {
            say "USB Wi-Fi disabled: $USB_WIFI_REAR_INTERFACE is missing"
            return 1
        }
        USB_WIFI_INTERFACE=$USB_WIFI_APPLICATION_INTERFACE
        say "USB Wi-Fi interface names already swapped"
        return 0
    else
        usb_wifi_stop_owned_supplicant || {
            say "USB Wi-Fi disabled: could not stop wpa_supplicant for interface swap"
            return 1
        }
        [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_SOURCE_INTERFACE" ] || {
            say "USB Wi-Fi disabled: $USB_WIFI_SOURCE_INTERFACE disappeared"
            return 1
        }
        [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_APPLICATION_INTERFACE" ] || {
            say "USB Wi-Fi disabled: rear $USB_WIFI_APPLICATION_INTERFACE is missing"
            return 1
        }
        [ ! -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || {
            say "USB Wi-Fi disabled: $USB_WIFI_REAR_INTERFACE already exists"
            return 1
        }

        ifconfig "$USB_WIFI_SOURCE_INTERFACE" down >/dev/null 2>&1 || return 1
        ifconfig "$USB_WIFI_APPLICATION_INTERFACE" down >/dev/null 2>&1 || {
            usb_wifi_restore_original_links
            return 1
        }
        "$USB_WIFI_IFRENAME" "$USB_WIFI_APPLICATION_INTERFACE" \
            "$USB_WIFI_REAR_INTERFACE" >/dev/null 2>&1 || {
            say "USB Wi-Fi disabled: could not preserve rear interface"
            usb_wifi_restore_original_links
            return 1
        }
        USB_WIFI_SWAP_CHANGED=1
        "$USB_WIFI_IFRENAME" "$USB_WIFI_SOURCE_INTERFACE" \
            "$USB_WIFI_APPLICATION_INTERFACE" >/dev/null 2>&1 || {
            say "USB Wi-Fi disabled: could not assign stock interface name"
            usb_wifi_rollback_swap || \
                say "USB Wi-Fi warning: rear interface name rollback failed"
            return 1
        }
    fi

    USB_WIFI_INTERFACE=$USB_WIFI_APPLICATION_INTERFACE
    ifconfig "$USB_WIFI_REAR_INTERFACE" up >/dev/null 2>&1 || \
        say "USB Wi-Fi warning: rear interface did not return after rename"
    if ! usb_wifi_start_supplicant || ! usb_wifi_wait_for_association; then
        say "USB Wi-Fi disabled: association failed after interface swap"
        usb_wifi_rollback_swap || \
            say "USB Wi-Fi warning: complete interface rollback failed"
        return 1
    fi
    say "USB Wi-Fi owns $USB_WIFI_APPLICATION_INTERFACE; rear USB-B is $USB_WIFI_REAR_INTERFACE"
}

usb_wifi_lan_address()
{
    ifconfig "$USB_WIFI_INTERFACE" 2>/dev/null | awk '
        {
            for (field = 1; field <= NF; field++) {
                address = ""
                if ($field ~ /^addr:/) {
                    address = $field
                    sub(/^addr:/, "", address)
                } else if ($field == "inet" && field < NF) {
                    address = $(field + 1)
                    sub(/^addr:/, "", address)
                }
                if (address != "" && address !~ /^169\.254\./ &&
                    address !~ /^127\./ && address != "0.0.0.0") {
                    print address
                    exit
                }
            }
        }
    '
}

usb_wifi_wait_for_dhcp()
{
    attempts=$1
    while [ "$attempts" -gt 0 ]; do
        USB_WIFI_ADDRESS=$(usb_wifi_lan_address)
        [ -n "$USB_WIFI_ADDRESS" ] && return 0
        sleep 1
        attempts=$((attempts - 1))
    done
    return 1
}

usb_wifi_dhcp_running()
{
    for process in /proc/[0-9]*; do
        [ "$(cat "$process/comm" 2>/dev/null)" = "udhcpc" ] || continue
        tr '\0' ' ' < "$process/cmdline" 2>/dev/null | \
            grep -Fq "udhcpc -i $USB_WIFI_INTERFACE" && return 0
    done
    return 1
}

usb_wifi_recover_dhcp()
{
    usb_wifi_dhcp_running && {
        say "USB Wi-Fi DHCP recovery deferred: stock client is still active"
        return 0
    }

    say "USB Wi-Fi DHCP recovery: retrying the stock command on $USB_WIFI_INTERFACE"
    udhcpc -i "$USB_WIFI_INTERFACE" -T 2 -t 3 -n -q >> "$USB_WIFI_LOG" 2>&1 || return 1
}

usb_wifi_finish_network()
{
    if usb_wifi_wait_for_dhcp "$USB_WIFI_DHCP_ATTEMPTS"; then
        USB_WIFI_DHCP_STATE=rbp-bound
        say "USB Wi-Fi lease acquired: $USB_WIFI_ADDRESS"
        return 0
    fi

    usb_wifi_recover_dhcp || {
        USB_WIFI_DHCP_STATE=retry-failed
        say "USB Wi-Fi warning: DHCP recovery command failed"
        return 1
    }

    if usb_wifi_wait_for_dhcp "$USB_WIFI_DHCP_RETRY_ATTEMPTS"; then
        USB_WIFI_DHCP_STATE=recovery-bound
        say "USB Wi-Fi recovery lease acquired: $USB_WIFI_ADDRESS"
        return 0
    fi

    USB_WIFI_DHCP_STATE=timed-out
    say "USB Wi-Fi warning: associated without a LAN DHCP lease"
    return 1
}

usb_wifi_prepare()
{
    [ -r "$CORE_OBJECT" ] || {
        say "USB Wi-Fi disabled: the RX3 runtime core is not selected"
        return 1
    }
    usb_wifi_install_firmware || return 1
    usb_wifi_load_modules || return 1
    usb_wifi_detect_interface || return 1
    usb_wifi_stage_runtime || return 1
    usb_wifi_start_supplicant || return 1
    usb_wifi_wait_for_association || {
        usb_wifi_stop_owned_supplicant >/dev/null 2>&1 || true
        return 1
    }
    request_rbp_restart
    say "USB Wi-Fi associated on $USB_WIFI_SOURCE_INTERFACE; interface swap is ready"
}

usb_wifi_after_launch()
{
    usb_wifi_finish_network
    network_status=$?
    {
        echo "interface=$USB_WIFI_INTERFACE"
        echo "application_interface=$USB_WIFI_APPLICATION_INTERFACE"
        echo "rear_interface=$USB_WIFI_REAR_INTERFACE"
        echo "usb_topology=$USB_WIFI_TOPOLOGY"
        echo "carrier=$(cat "$USB_WIFI_NET_SYSFS/$USB_WIFI_INTERFACE/carrier" 2>/dev/null)"
        echo "dhcp=$USB_WIFI_DHCP_STATE"
        echo "ipv4_address=${USB_WIFI_ADDRESS:-none}"
        ifconfig "$USB_WIFI_INTERFACE" 2>/dev/null
    } > "$USB_WIFI_STATE"
    return "$network_status"
}

usb_wifi_report()
{
    [ -r "$USB_WIFI_STATE" ] && cat "$USB_WIFI_STATE" >> "$LOG" 2>&1
    [ -r "$USB_WIFI_LOG" ] && tail -n 80 "$USB_WIFI_LOG" >> "$LOG" 2>&1
}

register_prepare_hook usb_wifi_prepare
register_stopped_hook usb_wifi_swap_interfaces
register_rollback_hook usb_wifi_rollback_swap
register_after_launch_hook usb_wifi_after_launch
register_report_hook usb_wifi_report
