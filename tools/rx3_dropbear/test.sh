#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

tool_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$tool_dir/build/dropbearmulti
image=localhost/rx3-dropbear-builder:bookworm

[ -f "$binary" ] || {
    echo "Build Dropbear first with tools/rx3_dropbear/build.sh" >&2
    exit 1
}

podman run --rm --network=none \
    -v "$binary:/dropbearmulti:ro" \
    "$image" sh -eu -c '
        header=$(arm-linux-gnueabi-readelf -h /dropbearmulti)
        printf "%s\n" "$header" | grep -q "Machine:.*ARM"
        printf "%s\n" "$header" | grep -q "Version5 EABI"
        printf "%s\n" "$header" | grep -q "soft-float ABI"
        arm-linux-gnueabi-readelf -d /dropbearmulti | \
            grep NEEDED && exit 1 || true

        algorithms=$(arm-linux-gnueabi-strings /dropbearmulti)
        for algorithm in \
            ssh-ed25519 \
            curve25519-sha256 \
            diffie-hellman-group14-sha256 \
            sntrup761x25519-sha512 \
            mlkem768x25519-sha256 \
            chacha20-poly1305@openssh.com \
            aes128-ctr \
            aes256-ctr
        do
            printf "%s\n" "$algorithms" | grep -Fxq "$algorithm"
        done

        qemu-arm -0 dropbearkey /dropbearmulti \
            -t ed25519 -f /tmp/hostkey >/tmp/keygen.log
        ssh-keygen -q -t ed25519 -N "" -f /tmp/clientkey
        mkdir -m 700 /root/.ssh
        cp /tmp/clientkey.pub /root/.ssh/authorized_keys
        chmod 600 /root/.ssh/authorized_keys

        qemu-arm -0 dropbear /dropbearmulti \
            -F -m -r /tmp/hostkey -D /root/.ssh \
            -p 127.0.0.1:2222 >/tmp/dropbear.log 2>&1 &
        daemon_pid=$!
        trap "kill $daemon_pid 2>/dev/null || true" EXIT

        attempt=0
        while [ "$attempt" -lt 20 ]; do
            if ssh \
                -o BatchMode=yes \
                -o StrictHostKeyChecking=no \
                -o UserKnownHostsFile=/dev/null \
                -o ConnectTimeout=1 \
                -i /tmp/clientkey \
                -p 2222 root@127.0.0.1 \
                "printf rx3-dropbear-ok" >/tmp/ssh.out 2>/tmp/ssh.err
            then
                break
            fi
            attempt=$((attempt + 1))
            sleep 0.2
        done

        if [ "$(cat /tmp/ssh.out)" != rx3-dropbear-ok ]; then
            cat /tmp/dropbear.log >&2
            cat /tmp/ssh.err >&2
            exit 1
        fi

        qemu-arm -0 dropbearkey /dropbearmulti -y -f /tmp/hostkey | \
            grep -q "^ssh-ed25519 "
        sha256sum /dropbearmulti
        wc -c /dropbearmulti
        cat /tmp/ssh.out
    '
