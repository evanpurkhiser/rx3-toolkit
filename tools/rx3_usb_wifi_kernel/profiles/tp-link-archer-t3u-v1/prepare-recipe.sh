#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
set -eu

source_directory=$1
recipe_directory=$2
archive=rtl88x2bu-f95349be51ccc3f11b53e21eef776721567c0109.tar.gz

mkdir -p "$recipe_directory/driver"
tar -xzf "$source_directory/$archive" --strip-components=1 \
    -C "$recipe_directory/driver"
