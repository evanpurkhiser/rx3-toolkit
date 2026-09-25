#!/bin/sh
set -eu

exec >/tmp/rx3-link-relaunch.log 2>&1
sleep 2

for process in /proc/[0-9]*; do
    [ "$(cat "$process/comm" 2>/dev/null)" = rbp ] || continue
    grep -q '^State:.*Z' "$process/status" 2>/dev/null && continue
    kill "${process#/proc/}"
done

while pidof rbp >/dev/null 2>&1; do
    sleep 1
done

cd /root/pdj
export LD_PRELOAD=/tmp/librx3_link_bootstrap.so
exec ./rbp
