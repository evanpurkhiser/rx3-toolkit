#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Replaces the running composite gadget between rbp instances, then exposes a
# passwordless root shell on the added CDC ACM function.

module_begin usb-serial usb_serial

USB_SERIAL_RELEASE=3.0.101-2790-gc248ed7-svn3098
USB_SERIAL_SRC=/mnt/iso/modules/usb-serial/g_pmulti_acm.ko
USB_SERIAL_MODULE=/tmp/rx3-g_pmulti-acm.ko
USB_SERIAL_LOGIN=/mnt/iso/modules/usb-serial/console-login.sh
USB_SERIAL_PID=/tmp/rx3-usb-serial-getty.pid
USB_SERIAL_BCD=""
USB_SERIAL_NUMBER=""

usb_serial_load_gadget()
{
    module_path=$1
    if [ -n "$USB_SERIAL_BCD" ] && [ -n "$USB_SERIAL_NUMBER" ]; then
        insmod "$module_path" bcdDevice="$USB_SERIAL_BCD" \
            iSerialNumber="$USB_SERIAL_NUMBER"
    elif [ -n "$USB_SERIAL_BCD" ]; then
        insmod "$module_path" bcdDevice="$USB_SERIAL_BCD"
    elif [ -n "$USB_SERIAL_NUMBER" ]; then
        insmod "$module_path" iSerialNumber="$USB_SERIAL_NUMBER"
    else
        insmod "$module_path"
    fi
}

usb_serial_restore_stock()
{
    stock=/lib/modules/$USB_SERIAL_RELEASE/kernel/drivers/usb/gadget/g_pmulti.ko
    [ -r "$stock" ] || {
        say "USB serial: stock g_pmulti module is unavailable"
        return 1
    }
    usb_serial_load_gadget "$stock" || {
        say "USB serial: stock g_pmulti restore failed"
        return 1
    }
    say "USB serial: restored stock composite gadget"
}

usb_serial_prepare()
{
    [ "$(uname -r 2>/dev/null)" = "$USB_SERIAL_RELEASE" ] || {
        say "USB serial disabled: kernel release does not match firmware 1.19"
        return 1
    }
    [ -r "$USB_SERIAL_SRC" ] || {
        say "USB serial disabled: composite module is missing"
        return 1
    }
    [ -x "$USB_SERIAL_LOGIN" ] || {
        say "USB serial disabled: console login helper is missing"
        return 1
    }

    if [ -c /dev/ttyGS0 ] && grep -q '^g_pmulti ' /proc/modules 2>/dev/null; then
        say "USB serial: composite ACM gadget is already active"
        return 0
    fi

    cp "$USB_SERIAL_SRC" "$USB_SERIAL_MODULE" 2>/dev/null || return 1
    chmod 600 "$USB_SERIAL_MODULE"

    USB_SERIAL_NUMBER=$(fw_printenv -n serial_num 2>/dev/null)
    USB_SERIAL_BCD=$(
        cd /root/pdj 2>/dev/null || exit
        . ./setup_revision.sh 2>/dev/null || exit
        setup_release >/dev/null 2>&1
        ./str2hex "$release" 2>/dev/null
    )

    request_rbp_restart
    say "USB serial prepared: composite gadget swap requested"
}

usb_serial_swap_gadget()
{
    grep -q '^g_pmulti ' /proc/modules 2>/dev/null || {
        say "USB serial: stock g_pmulti is not loaded"
        return 1
    }

    rmmod g_pmulti >/dev/null 2>&1 || {
        say "USB serial: stock g_pmulti remained busy after rbp stopped"
        return 1
    }

    if ! usb_serial_load_gadget "$USB_SERIAL_MODULE" >/dev/null 2>&1; then
        say "USB serial: composite ACM module failed to load"
        usb_serial_restore_stock
        return 1
    fi

    tty_wait=0
    while [ "$tty_wait" -lt 5 ] && [ ! -c /dev/ttyGS0 ]; do
        sleep 1
        tty_wait=$((tty_wait + 1))
    done
    if [ ! -c /dev/ttyGS0 ]; then
        say "USB serial: ttyGS0 was not created"
        rmmod g_pmulti >/dev/null 2>&1
        usb_serial_restore_stock
        return 1
    fi

    say "USB serial: composite audio/MIDI/HID/ACM gadget active"
}

usb_serial_start_console()
{
    if [ -r "$USB_SERIAL_PID" ]; then
        console_pid=$(cat "$USB_SERIAL_PID" 2>/dev/null)
        [ -n "$console_pid" ] && [ -d "/proc/$console_pid" ] && {
            say "USB serial: console already active on ttyGS0"
            return 0
        }
    fi

    (
        while [ -c /dev/ttyGS0 ] && grep -q '^g_pmulti ' /proc/modules 2>/dev/null; do
            /bin/busybox getty -n -l "$USB_SERIAL_LOGIN" -L \
                ttyGS0 115200 vt100 >/dev/null 2>&1
            sleep 1
        done
    ) &
    console_pid=$!
    echo "$console_pid" > "$USB_SERIAL_PID"
    sleep 1
    [ -d "/proc/$console_pid" ] || {
        say "USB serial: getty exited before a host connected"
        return 1
    }
    say "USB serial: passwordless root console waiting on ttyGS0"
}

register_prepare_hook usb_serial_prepare
register_stopped_hook usb_serial_swap_gadget
register_after_launch_hook usb_serial_start_console
