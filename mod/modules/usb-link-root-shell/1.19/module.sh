#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Volatile passwordless root access over the stock USB Link Export network.

module_begin usb-link-root-shell usb_link_root_shell

USB_LINK_ROOT_SHELL_PID=/tmp/rx3-usb-link-root-shell.pid
USB_LINK_ROOT_SHELL_INTERFACE=eth0
USB_LINK_ROOT_SHELL_ALIAS=eth0:rx3shell
USB_LINK_ROOT_SHELL_ADDRESS=169.254.100.2

usb_link_root_shell_address_ready()
{
    ifconfig "$USB_LINK_ROOT_SHELL_ALIAS" 2>/dev/null |
        grep -q "inet addr:$USB_LINK_ROOT_SHELL_ADDRESS"
}

usb_link_root_shell_configure_address()
{
    if usb_link_root_shell_address_ready; then
        return 0
    fi

    if ! ifconfig "$USB_LINK_ROOT_SHELL_INTERFACE" >/dev/null 2>&1; then
        say "USB Link root shell disabled: $USB_LINK_ROOT_SHELL_INTERFACE is unavailable"
        return 1
    fi

    if ! ifconfig "$USB_LINK_ROOT_SHELL_ALIAS" "$USB_LINK_ROOT_SHELL_ADDRESS" \
        netmask 255.255.0.0 up >/dev/null 2>&1 ||
       ! usb_link_root_shell_address_ready; then
        say "USB Link root shell disabled: could not add $USB_LINK_ROOT_SHELL_ADDRESS to $USB_LINK_ROOT_SHELL_INTERFACE"
        return 1
    fi

    say "USB Link root shell address: $USB_LINK_ROOT_SHELL_ADDRESS/16"
}

usb_link_root_shell_listening()
{
    awk '
        NR > 1 {
            split($2, local, ":")
            if (toupper(local[2]) == "0017" && toupper($4) == "0A")
                found = 1
        }
        END { exit !found }
    ' /proc/net/tcp 2>/dev/null
}

usb_link_root_shell_owned_listener()
{
    [ -r "$USB_LINK_ROOT_SHELL_PID" ] || return 1
    shell_pid=$(cat "$USB_LINK_ROOT_SHELL_PID" 2>/dev/null)
    case "$shell_pid" in
        ""|*[!0-9]*) return 1 ;;
    esac
    kill -0 "$shell_pid" 2>/dev/null || return 1
    tr '\0' ' ' < "/proc/$shell_pid/cmdline" 2>/dev/null |
        grep -q 'telnetd.*-l /bin/sh' || return 1
    usb_link_root_shell_listening
}

usb_link_root_shell_start()
{
    usb_link_root_shell_configure_address || return 1

    if usb_link_root_shell_owned_listener; then
        say "USB Link root shell remains active from the previous insertion"
        return 0
    fi

    rm -f "$USB_LINK_ROOT_SHELL_PID"
    if usb_link_root_shell_listening; then
        say "USB Link root shell disabled: TCP port 23 is already open"
        return 1
    fi

    /bin/busybox telnetd -F -p 23 -l /bin/sh >/dev/null 2>&1 &
    shell_pid=$!
    echo "$shell_pid" > "$USB_LINK_ROOT_SHELL_PID"

    sleep 1
    if ! kill -0 "$shell_pid" 2>/dev/null ||
       ! usb_link_root_shell_listening; then
        kill "$shell_pid" 2>/dev/null
        wait "$shell_pid" 2>/dev/null
        rm -f "$USB_LINK_ROOT_SHELL_PID"
        say "USB Link root shell disabled: telnetd did not open TCP port 23"
        return 1
    fi

    say "WARNING: unauthenticated root shell active on USB Link Export"
    say "Connect with: telnet $USB_LINK_ROOT_SHELL_ADDRESS 23"
}

register_prepare_hook usb_link_root_shell_start
