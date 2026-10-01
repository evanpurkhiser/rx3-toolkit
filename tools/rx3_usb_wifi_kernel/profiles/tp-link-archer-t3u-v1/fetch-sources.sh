#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

destination=${1:-sources}
revision=f95349be51ccc3f11b53e21eef776721567c0109
archive=rtl88x2bu-$revision.tar.gz
expected=a07d60f4d79cd8ae7c5ba2f724ee72ce998218b5603709373e8585e5a3e01639
url=https://github.com/RinCat/RTL88x2BU-Linux-Driver/archive/$revision.tar.gz
output=$destination/$archive

mkdir -p "$destination"
if [ -f "$output" ] && [ "$(sha256sum "$output" | awk '{print $1}')" = "$expected" ]; then
    exit 0
fi

temporary=$output.tmp.$$
trap 'rm -f "$temporary"' EXIT HUP INT TERM
curl -fL --retry 3 -o "$temporary" "$url"
actual=$(sha256sum "$temporary" | awk '{print $1}')
[ "$actual" = "$expected" ] || {
    echo "checksum mismatch for $url: $actual" >&2
    exit 1
}
mv "$temporary" "$output"
trap - EXIT HUP INT TERM
