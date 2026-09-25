#!/bin/sh
set -eu

output=${1:-/tmp/rx3-link-source-state.log}
samples=${2:-12000}

pid=
for process in /proc/[0-9]*; do
    [ "$(cat "$process/comm" 2>/dev/null)" = rbp ] || continue
    grep -q '^State:.*Z' "$process/status" 2>/dev/null && continue
    pid=${process#/proc/}
done

[ -n "$pid" ]

read32() {
    dd if="/proc/$pid/mem" bs=1 skip="$1" count=4 2>/dev/null |
        hexdump -v -e '1/4 "%u"'
}

previous=
index=0
: >"$output"
while [ "$index" -lt "$samples" ]; do
    current="mask=$(read32 $((0x0326f8b4))) pc=$(read32 $((0x0326f904))) cert=$(dd if=/proc/$pid/mem bs=1 skip=$((0x026870d0)) count=1 2>/dev/null | hexdump -v -e '1/1 "%u"') d11=$(read32 $((0x0325b818))) d12=$(read32 $((0x0325bcb0))) d29=$(read32 $((0x03262658))) d2a=$(read32 $((0x03262af0))) d2b=$(read32 $((0x03262f88))) d2c=$(read32 $((0x03263420)))"
    if [ "$current" != "$previous" ]; then
        echo "$(date +%s) $current" >>"$output"
        previous=$current
    fi
    index=$((index + 1))
    usleep 100000
done
