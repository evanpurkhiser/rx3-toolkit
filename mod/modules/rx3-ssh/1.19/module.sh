#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Authenticated root access over any configured RX3 network interface.

module_begin rx3-ssh rx3_ssh

RX3_SSH_RUNTIME=/tmp/rx3-ssh
RX3_SSH_EXEC_RUNTIME=/dev/shm/rx3-ssh
RX3_SSH_EXECUTABLE=$RX3_SSH_EXEC_RUNTIME/dropbearmulti
RX3_SSH_DROPBEAR=$RX3_SSH_EXEC_RUNTIME/dropbear
RX3_SSH_DROPBEARKEY=$RX3_SSH_EXEC_RUNTIME/dropbearkey
RX3_SSH_HOST_KEY=$RX3_SSH_RUNTIME/dropbear_ed25519_host_key
RX3_SSH_CONFIG=$USB/RX3_SSH
RX3_SSH_SOURCE=/mnt/iso/modules/rx3-ssh/dropbearmulti
RX3_SSH_SOURCE_SHA1=0b2085f0ff4404260fc585291e27041c859f7d15
RX3_SSH_AUTH_SOURCE=$RX3_SSH_CONFIG/authorized_keys
RX3_SSH_PERSISTED_HOST_KEY=$RX3_SSH_CONFIG/dropbear_ed25519_host_key
RX3_SSH_AUTH_DIRECTORY=/root/.ssh
RX3_SSH_AUTH_TARGET=$RX3_SSH_AUTH_DIRECTORY/authorized_keys
RX3_SSH_PID=/tmp/rx3-ssh.pid
RX3_SSH_LOG=/tmp/rx3-ssh.log
RX3_SSH_STATE=/tmp/rx3-ssh.state
RX3_SSH_PORT=22
RX3_SSH_PORT_HEX=0016

rx3_ssh_port_listening()
{
    awk -v port="$RX3_SSH_PORT_HEX" '
        NR > 1 {
            split($2, local, ":")
            if (toupper(local[2]) == port && toupper($4) == "0A")
                found = 1
        }
        END { exit !found }
    ' /proc/net/tcp 2>/dev/null
}

rx3_ssh_owned_listener()
{
    [ -r "$RX3_SSH_PID" ] || return 1
    ssh_pid=$(cat "$RX3_SSH_PID" 2>/dev/null)
    case "$ssh_pid" in
        ""|*[!0-9]*) return 1 ;;
    esac
    kill -0 "$ssh_pid" 2>/dev/null || return 1
    tr '\0' ' ' < "/proc/$ssh_pid/cmdline" 2>/dev/null |
        grep -F -q "$RX3_SSH_DROPBEAR" || return 1
    rx3_ssh_port_listening
}

rx3_ssh_install_tools()
{
    [ -r "$RX3_SSH_SOURCE" ] || {
        say "RX3 SSH disabled: executable $RX3_SSH_SOURCE is missing"
        return 1
    }

    source_sha1=$(sha1sum "$RX3_SSH_SOURCE" 2>/dev/null | awk '{print $1}')
    [ "$source_sha1" = "$RX3_SSH_SOURCE_SHA1" ] || {
        say "RX3 SSH disabled: Dropbear checksum does not match this module"
        return 1
    }

    mkdir -p "$RX3_SSH_RUNTIME" "$RX3_SSH_EXEC_RUNTIME" || return 1
    chmod 700 "$RX3_SSH_RUNTIME" "$RX3_SSH_EXEC_RUNTIME" || return 1

    executable_temporary=$RX3_SSH_EXECUTABLE.$$
    rm -f "$executable_temporary"
    cp "$RX3_SSH_SOURCE" "$executable_temporary" || return 1
    chmod 700 "$executable_temporary" || {
        rm -f "$executable_temporary"
        return 1
    }
    executable_sha1=$(sha1sum "$executable_temporary" 2>/dev/null | awk '{print $1}')
    [ "$executable_sha1" = "$RX3_SSH_SOURCE_SHA1" ] || {
        rm -f "$executable_temporary"
        say "RX3 SSH disabled: RAM copy checksum does not match this module"
        return 1
    }
    mv -f "$executable_temporary" "$RX3_SSH_EXECUTABLE" || {
        rm -f "$executable_temporary"
        return 1
    }

    ln -sf "$RX3_SSH_EXECUTABLE" "$RX3_SSH_DROPBEAR" || return 1
    ln -sf "$RX3_SSH_EXECUTABLE" "$RX3_SSH_DROPBEARKEY" || return 1
}

rx3_ssh_authorized_keys_valid()
{
    [ -s "$RX3_SSH_AUTH_SOURCE" ] || return 1
    awk '
        /^[[:space:]]*(#|$)/ { next }
        $1 != "ssh-ed25519" { invalid = 1 }
        { keys++ }
        END { exit invalid || keys == 0 }
    ' "$RX3_SSH_AUTH_SOURCE"
}

rx3_ssh_install_authorized_keys()
{
    rx3_ssh_authorized_keys_valid || {
        say "RX3 SSH disabled: $RX3_SSH_AUTH_SOURCE needs at least one plain ssh-ed25519 key"
        return 1
    }

    # Stock firmware ships /root as 0775. Dropbear correctly rejects keys
    # below a group-writable home, so tighten the RAM-backed directory first.
    chmod go-w /root || return 1
    mkdir -p "$RX3_SSH_AUTH_DIRECTORY" || return 1
    chmod 700 "$RX3_SSH_AUTH_DIRECTORY" || return 1
    auth_temporary=$RX3_SSH_AUTH_TARGET.$$
    cp "$RX3_SSH_AUTH_SOURCE" "$auth_temporary" || return 1
    chown 0:0 "$auth_temporary" 2>/dev/null || return 1
    chmod 600 "$auth_temporary" || return 1
    mv -f "$auth_temporary" "$RX3_SSH_AUTH_TARGET" || {
        rm -f "$auth_temporary"
        return 1
    }
}

rx3_ssh_host_key_valid()
{
    [ -s "$1" ] || return 1
    "$RX3_SSH_DROPBEARKEY" -y -f "$1" >/dev/null 2>&1
}

rx3_ssh_prepare_host_key()
{
    host_key_source=generated
    rm -f "$RX3_SSH_HOST_KEY"

    if [ -s "$RX3_SSH_PERSISTED_HOST_KEY" ]; then
        cp "$RX3_SSH_PERSISTED_HOST_KEY" "$RX3_SSH_HOST_KEY" || return 1
        chmod 600 "$RX3_SSH_HOST_KEY" || return 1
        rx3_ssh_host_key_valid "$RX3_SSH_HOST_KEY" || {
            say "RX3 SSH disabled: persisted host key is invalid"
            return 1
        }
        host_key_source=persisted
        return 0
    fi

    "$RX3_SSH_DROPBEARKEY" -t ed25519 -f "$RX3_SSH_HOST_KEY" \
        > "$RX3_SSH_RUNTIME/host-key.log" 2>&1 || return 1
    chmod 600 "$RX3_SSH_HOST_KEY" || return 1
    rx3_ssh_host_key_valid "$RX3_SSH_HOST_KEY" || return 1

    persisted_temporary=$RX3_SSH_PERSISTED_HOST_KEY.$$
    if cp "$RX3_SSH_HOST_KEY" "$persisted_temporary" 2>/dev/null &&
       mv -f "$persisted_temporary" "$RX3_SSH_PERSISTED_HOST_KEY" 2>/dev/null; then
        host_key_source=generated-and-persisted
        sync
    else
        rm -f "$persisted_temporary"
        say "RX3 SSH host key is volatile: $RX3_SSH_CONFIG is not writable"
    fi
}

rx3_ssh_write_state()
{
    key_count=$(awk '/^[[:space:]]*(#|$)/ { next } { count++ } END { print count+0 }' \
        "$RX3_SSH_AUTH_TARGET")
    fingerprint=$("$RX3_SSH_DROPBEARKEY" -y -f "$RX3_SSH_HOST_KEY" 2>/dev/null |
        awk -F': ' '/Fingerprint:/ { print $2; exit }')
    {
        echo "listen=0.0.0.0:$RX3_SSH_PORT"
        echo "authentication=public-key-only"
        echo "authorized_keys=$key_count"
        echo "host_key=$host_key_source"
        echo "host_fingerprint=$fingerprint"
        echo "pid=$(cat "$RX3_SSH_PID" 2>/dev/null)"
    } > "$RX3_SSH_STATE"
}

rx3_ssh_start()
{
    rx3_ssh_install_tools || return 1
    rx3_ssh_install_authorized_keys || return 1

    if rx3_ssh_owned_listener; then
        say "RX3 SSH remains active from the previous insertion"
        return 0
    fi

    rm -f "$RX3_SSH_PID" "$RX3_SSH_LOG" "$RX3_SSH_STATE"
    if rx3_ssh_port_listening; then
        say "RX3 SSH disabled: TCP port $RX3_SSH_PORT is already open"
        return 1
    fi

    rx3_ssh_prepare_host_key || {
        say "RX3 SSH disabled: could not prepare Ed25519 host key"
        return 1
    }

    (
        cd /
        exec "$RX3_SSH_DROPBEAR" -F -s -g -m \
            -D "$RX3_SSH_AUTH_DIRECTORY" \
            -P "$RX3_SSH_PID" \
            -r "$RX3_SSH_HOST_KEY" \
            -p "0.0.0.0:$RX3_SSH_PORT"
    ) > "$RX3_SSH_LOG" 2>&1 &
    ssh_pid=$!
    sleep 1
    if ! kill -0 "$ssh_pid" 2>/dev/null || ! rx3_ssh_port_listening; then
        kill "$ssh_pid" 2>/dev/null
        wait "$ssh_pid" 2>/dev/null
        rm -f "$RX3_SSH_PID"
        say "RX3 SSH disabled: Dropbear did not open TCP port $RX3_SSH_PORT"
        cat "$RX3_SSH_LOG" >> "$LOG" 2>/dev/null
        return 1
    fi

    rx3_ssh_write_state
    say "RX3 SSH active: public-key-only root login on TCP port $RX3_SSH_PORT"
}

rx3_ssh_report()
{
    [ -r "$RX3_SSH_STATE" ] && cat "$RX3_SSH_STATE" >> "$LOG" 2>&1
    [ -r "$RX3_SSH_LOG" ] && cat "$RX3_SSH_LOG" >> "$LOG" 2>&1
}

register_prepare_hook rx3_ssh_start
register_report_hook rx3_ssh_report
