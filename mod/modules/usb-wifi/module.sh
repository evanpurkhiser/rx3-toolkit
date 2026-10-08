#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Native Wi-Fi bring-up for a selected USB adapter profile.

module_begin usb-wifi usb_wifi

: "${USB_WIFI_USB_SYSFS:=/sys/bus/usb/devices}"
: "${USB_WIFI_USB_DRIVER:=/sys/bus/usb/drivers/usb}"
: "${USB_WIFI_NET_SYSFS:=/sys/class/net}"
: "${USB_WIFI_PROC_MODULES:=/proc/modules}"
: "${USB_WIFI_CONFIG:=$USB/RX3_WIFI/wpa_supplicant.conf}"
: "${USB_WIFI_STATE:=/tmp/rx3-usb-wifi.state}"

USB_WIFI_DIRECTORY=/mnt/iso/modules/usb-wifi
USB_WIFI_HARDWARE_DIRECTORY=$USB_WIFI_DIRECTORY/hardware
USB_WIFI_PROFILE=$USB_WIFI_HARDWARE_DIRECTORY/profile.json
USB_WIFI_IDS=$USB_WIFI_HARDWARE_DIRECTORY/usb.ids
USB_WIFI_MODULES=$USB_WIFI_HARDWARE_DIRECTORY/modules.load
USB_WIFI_FIRMWARE=$USB_WIFI_HARDWARE_DIRECTORY/firmware.list
USB_WIFI_SUPPLICANT_DRIVER_FILE=$USB_WIFI_HARDWARE_DIRECTORY/supplicant.driver
USB_WIFI_FIRMWARE_DIRECTORY=/lib/firmware
: "${USB_WIFI_RUNTIME_DIRECTORY:=/dev/shm/rx3-usb-wifi}"
USB_WIFI_SOURCE_INTERFACE=
USB_WIFI_INTERFACE=
USB_WIFI_APPLICATION_INTERFACE=eth0
USB_WIFI_REAR_INTERFACE=usb0
USB_WIFI_REAR_ADDRESS=169.254.100.2
USB_WIFI_REAR_NETMASK=255.255.0.0
USB_WIFI_TOPOLOGY=
USB_WIFI_PID_FILE=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.pid
: "${USB_WIFI_LOG:=/tmp/rx3-usb-wifi.log}"
USB_WIFI_RUNTIME_CONFIG=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant.conf
USB_WIFI_SUPPLICANT=$USB_WIFI_RUNTIME_DIRECTORY/wpa_supplicant
USB_WIFI_CLI=$USB_WIFI_RUNTIME_DIRECTORY/wpa_cli
USB_WIFI_IFRENAME=$USB_WIFI_RUNTIME_DIRECTORY/rx3-ifrename
USB_WIFI_PROFILE_ID=
USB_WIFI_MATCHED_ID=
USB_WIFI_SUPPLICANT_DRIVER=
USB_WIFI_DHCP_ATTEMPTS=25
USB_WIFI_DHCP_RETRY_ATTEMPTS=10
USB_WIFI_RESET_ATTEMPTS=10
USB_WIFI_ADDRESS=
USB_WIFI_DHCP_STATE=pending
USB_WIFI_SWAP_CHANGED=0

register_diagnostic_file "$USB_WIFI_LOG"
register_menu_item RX3-TOOLKIT "WIFI CONNECTED" field \
    "$USB_WIFI_STATE" wifi_connected
register_menu_item RX3-TOOLKIT "WIFI ADDRESS" field \
    "$USB_WIFI_STATE" wifi_address

usb_wifi_read_attribute()
{
    tr -d '\r\n' < "$1" 2>/dev/null
}

usb_wifi_module_loaded()
{
    awk -v wanted="$1" '$1 == wanted { found = 1 } END { exit !found }' \
        "$USB_WIFI_PROC_MODULES" 2>/dev/null
}

usb_wifi_load_profile()
{
    for required in "$USB_WIFI_PROFILE" "$USB_WIFI_IDS" "$USB_WIFI_MODULES" \
        "$USB_WIFI_FIRMWARE" "$USB_WIFI_SUPPLICANT_DRIVER_FILE"; do
        [ -r "$required" ] || {
            say "USB Wi-Fi disabled: incomplete hardware profile"
            return 1
        }
    done

    USB_WIFI_PROFILE_ID=$(sed -n \
        's/^[[:space:]]*"id"[[:space:]]*:[[:space:]]*"\([a-z0-9-]*\)".*/\1/p' \
        "$USB_WIFI_PROFILE" | head -n 1)
    USB_WIFI_SUPPLICANT_DRIVER=$(tr -d '\r\n' < "$USB_WIFI_SUPPLICANT_DRIVER_FILE")
    [ -n "$USB_WIFI_PROFILE_ID" ] && \
        [ -n "$USB_WIFI_SUPPLICANT_DRIVER" ] || {
        say "USB Wi-Fi disabled: invalid hardware profile"
        return 1
    }
}

usb_wifi_install_firmware()
{
    while read -r source target; do
        case "$source" in ''|\#*) continue ;; esac
        case "$source:$target" in *..*|/*:*|*:/*) return 1 ;; esac
        destination=$USB_WIFI_FIRMWARE_DIRECTORY/$target
        mkdir -p "${destination%/*}" || return 1
        cp "$USB_WIFI_HARDWARE_DIRECTORY/$source" "$destination" || return 1
        chmod 644 "$destination" || return 1
    done < "$USB_WIFI_FIRMWARE"
}

usb_wifi_load_one()
{
    module_name=$1
    module_file=$2
    usb_wifi_module_loaded "$module_name" && return 0
    insmod "$USB_WIFI_HARDWARE_DIRECTORY/$module_file" >/dev/null 2>&1 || {
        say "USB Wi-Fi disabled: $module_file did not load"
        return 1
    }
}

usb_wifi_load_modules()
{
    while read -r module_name module_file; do
        case "$module_name" in ''|\#*) continue ;; esac
        usb_wifi_load_one "$module_name" "$module_file" || return 1
    done < "$USB_WIFI_MODULES"
}

usb_wifi_id_supported()
{
    grep -Fxiq "$1:$2" "$USB_WIFI_IDS"
}

usb_wifi_detect_interface()
{
    matches=0
    matched_interface=
    matched_topology=

    for usb_device in "$USB_WIFI_USB_SYSFS"/*; do
        [ -f "$usb_device/idVendor" ] || continue
        vendor=$(usb_wifi_read_attribute "$usb_device/idVendor")
        product=$(usb_wifi_read_attribute "$usb_device/idProduct")
        usb_wifi_id_supported "$vendor" "$product" || continue

        for net_path in "$usb_device"/net/* "$usb_device":*/net/*; do
            [ -e "$net_path" ] || continue
            interface=${net_path##*/}
            [ -e "$USB_WIFI_NET_SYSFS/$interface" ] || continue
            matches=$((matches + 1))
            matched_interface=$interface
            matched_topology=${usb_device##*/}
            USB_WIFI_MATCHED_ID=$vendor:$product
        done
    done

    [ "$matches" = "1" ] || {
        say "USB Wi-Fi disabled: profile $USB_WIFI_PROFILE_ID found $matches interfaces"
        return 1
    }

    USB_WIFI_SOURCE_INTERFACE=$matched_interface
    USB_WIFI_INTERFACE=$matched_interface
    USB_WIFI_TOPOLOGY=$matched_topology
}

usb_wifi_stage_config()
{
    cp "$USB_WIFI_CONFIG" "$USB_WIFI_RUNTIME_CONFIG" || return 1
    if grep -Eq '^[[:space:]]*ctrl_interface=' "$USB_WIFI_RUNTIME_CONFIG"; then
        sed -i \
            "s#^[[:space:]]*ctrl_interface=.*#ctrl_interface=$USB_WIFI_RUNTIME_DIRECTORY/control#" \
            "$USB_WIFI_RUNTIME_CONFIG" || return 1
    else
        {
            echo "ctrl_interface=$USB_WIFI_RUNTIME_DIRECTORY/control"
            cat "$USB_WIFI_RUNTIME_CONFIG"
        } > "$USB_WIFI_RUNTIME_CONFIG.tmp" || return 1
        mv -f "$USB_WIFI_RUNTIME_CONFIG.tmp" "$USB_WIFI_RUNTIME_CONFIG" || return 1
    fi
    chmod 600 "$USB_WIFI_RUNTIME_CONFIG"
}

usb_wifi_stage_runtime()
{
    [ -r "$USB_WIFI_CONFIG" ] || {
        say "USB Wi-Fi disabled: missing $USB_WIFI_CONFIG"
        return 1
    }
    mkdir -p "$USB_WIFI_RUNTIME_DIRECTORY" || return 1
    cp "$USB_WIFI_HARDWARE_DIRECTORY/wpa_supplicant" "$USB_WIFI_SUPPLICANT" || return 1
    cp "$USB_WIFI_HARDWARE_DIRECTORY/wpa_cli" "$USB_WIFI_CLI" || return 1
    cp "$USB_WIFI_HARDWARE_DIRECTORY/rx3-ifrename" "$USB_WIFI_IFRENAME" || return 1
    usb_wifi_stage_config || return 1
    chmod 700 "$USB_WIFI_SUPPLICANT" "$USB_WIFI_CLI" "$USB_WIFI_IFRENAME" || return 1
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
    "$USB_WIFI_SUPPLICANT" -D"$USB_WIFI_SUPPLICANT_DRIVER" -i"$USB_WIFI_INTERFACE" \
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

usb_wifi_reset_adapter()
{
    usb_wifi_stop_owned_supplicant || return 1
    case "$USB_WIFI_TOPOLOGY" in
        ""|*[!0-9.-]*) return 1 ;;
    esac
    [ -w "$USB_WIFI_USB_DRIVER/unbind" ] || return 1
    [ -w "$USB_WIFI_USB_DRIVER/bind" ] || return 1

    printf '%s' "$USB_WIFI_TOPOLOGY" > "$USB_WIFI_USB_DRIVER/unbind" || return 1
    sleep 2
    printf '%s' "$USB_WIFI_TOPOLOGY" > "$USB_WIFI_USB_DRIVER/bind" || return 1

    attempts=$USB_WIFI_RESET_ATTEMPTS
    while [ "$attempts" -gt 0 ]; do
        for net_path in \
            "$USB_WIFI_USB_SYSFS/$USB_WIFI_TOPOLOGY"/net/* \
            "$USB_WIFI_USB_SYSFS/$USB_WIFI_TOPOLOGY":*/net/*; do
            [ -e "$net_path" ] || continue
            rebound_interface=${net_path##*/}
            [ -e "$USB_WIFI_NET_SYSFS/$rebound_interface" ] || continue
            USB_WIFI_SOURCE_INTERFACE=$rebound_interface
            USB_WIFI_INTERFACE=$rebound_interface
            return 0
        done
        sleep 1
        attempts=$((attempts - 1))
    done
    return 1
}

usb_wifi_associate()
{
    if usb_wifi_start_supplicant && usb_wifi_wait_for_association; then
        return 0
    fi

    say "USB Wi-Fi association retry: resetting $USB_WIFI_TOPOLOGY"
    usb_wifi_reset_adapter || return 1
    if usb_wifi_start_supplicant && usb_wifi_wait_for_association; then
        return 0
    fi

    usb_wifi_stop_owned_supplicant >/dev/null 2>&1 || true
    return 1
}

usb_wifi_wpa_state()
{
    "$USB_WIFI_CLI" -p "$USB_WIFI_RUNTIME_DIRECTORY/control" \
        -i "$USB_WIFI_INTERFACE" status 2>/dev/null | awk -F= \
        '$1 == "wpa_state" { print $2; exit }'
}

usb_wifi_existing_association()
{
    [ "$USB_WIFI_SOURCE_INTERFACE" = "$USB_WIFI_APPLICATION_INTERFACE" ] || return 1
    [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || return 1
    [ "$(cat "$USB_WIFI_NET_SYSFS/$USB_WIFI_SOURCE_INTERFACE/carrier" 2>/dev/null)" = "1" ] || return 1

    USB_WIFI_INTERFACE=$USB_WIFI_SOURCE_INTERFACE
    [ "$(usb_wifi_wpa_state)" = "COMPLETED" ]
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

usb_wifi_configure_rear_interface()
{
    [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || return 1
    ifconfig "$USB_WIFI_REAR_INTERFACE" "$USB_WIFI_REAR_ADDRESS" \
        netmask "$USB_WIFI_REAR_NETMASK" up >/dev/null 2>&1 || return 1
    ifconfig "$USB_WIFI_REAR_INTERFACE" 2>/dev/null |
        grep -q "inet addr:$USB_WIFI_REAR_ADDRESS"
}

usb_wifi_swap_interfaces()
{
    if [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] &&
       [ ! -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_APPLICATION_INTERFACE" ] &&
       [ "$USB_WIFI_SOURCE_INTERFACE" != "$USB_WIFI_APPLICATION_INTERFACE" ]; then
        usb_wifi_stop_owned_supplicant || return 1
        ifconfig "$USB_WIFI_SOURCE_INTERFACE" down >/dev/null 2>&1 || return 1
        "$USB_WIFI_IFRENAME" "$USB_WIFI_SOURCE_INTERFACE" \
            "$USB_WIFI_APPLICATION_INTERFACE" >/dev/null 2>&1 || return 1
        USB_WIFI_INTERFACE=$USB_WIFI_APPLICATION_INTERFACE
        if ! usb_wifi_start_supplicant || ! usb_wifi_wait_for_association; then
            ifconfig "$USB_WIFI_APPLICATION_INTERFACE" down >/dev/null 2>&1 || true
            "$USB_WIFI_IFRENAME" "$USB_WIFI_APPLICATION_INTERFACE" \
                "$USB_WIFI_SOURCE_INTERFACE" >/dev/null 2>&1 || true
            USB_WIFI_INTERFACE=$USB_WIFI_SOURCE_INTERFACE
            return 1
        fi
        usb_wifi_configure_rear_interface || \
            say "USB Wi-Fi warning: rear USB-B management address is unavailable"
        say "USB Wi-Fi restored $USB_WIFI_APPLICATION_INTERFACE after adapter reset"
        return 0
    elif [ "$USB_WIFI_SOURCE_INTERFACE" = "$USB_WIFI_APPLICATION_INTERFACE" ]; then
        [ -e "$USB_WIFI_NET_SYSFS/$USB_WIFI_REAR_INTERFACE" ] || {
            say "USB Wi-Fi disabled: $USB_WIFI_REAR_INTERFACE is missing"
            return 1
        }
        USB_WIFI_INTERFACE=$USB_WIFI_APPLICATION_INTERFACE
        usb_wifi_configure_rear_interface || \
            say "USB Wi-Fi warning: rear USB-B management address is unavailable"
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
    usb_wifi_configure_rear_interface || \
        say "USB Wi-Fi warning: rear USB-B management address is unavailable"
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

usb_wifi_write_state()
{
    wifi_connected=NO
    [ "$(usb_wifi_wpa_state)" = "COMPLETED" ] && wifi_connected=YES

    {
        echo "hardware_profile=$USB_WIFI_PROFILE_ID"
        echo "usb_id=$USB_WIFI_MATCHED_ID"
        echo "supplicant_driver=$USB_WIFI_SUPPLICANT_DRIVER"
        echo "interface=$USB_WIFI_INTERFACE"
        echo "application_interface=$USB_WIFI_APPLICATION_INTERFACE"
        echo "rear_interface=$USB_WIFI_REAR_INTERFACE"
        echo "rear_ipv4=$USB_WIFI_REAR_ADDRESS"
        echo "usb_topology=$USB_WIFI_TOPOLOGY"
        echo "carrier=$(cat "$USB_WIFI_NET_SYSFS/$USB_WIFI_INTERFACE/carrier" 2>/dev/null)"
        echo "wifi_connected=$wifi_connected"
        echo "wifi_address=$USB_WIFI_ADDRESS"
        echo "dhcp=$USB_WIFI_DHCP_STATE"
        echo "ipv4_address=${USB_WIFI_ADDRESS:-none}"
        ifconfig "$USB_WIFI_INTERFACE" 2>/dev/null
    } > "$USB_WIFI_STATE.tmp" || return 1

    mv -f "$USB_WIFI_STATE.tmp" "$USB_WIFI_STATE"
}

usb_wifi_prepare()
{
    usb_wifi_load_profile || return 1
    usb_wifi_install_firmware || return 1
    usb_wifi_load_modules || return 1
    usb_wifi_detect_interface || return 1
    usb_wifi_stage_runtime || return 1
    if usb_wifi_existing_association; then
        say "USB Wi-Fi already associated on $USB_WIFI_APPLICATION_INTERFACE"
        return 0
    fi
    usb_wifi_associate || return 1
    request_rbp_restart
    say "USB Wi-Fi associated on $USB_WIFI_SOURCE_INTERFACE; interface swap is ready"
}

usb_wifi_after_launch()
{
    usb_wifi_finish_network
    network_status=$?
    usb_wifi_write_state || return 1
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
