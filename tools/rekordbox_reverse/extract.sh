#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 INSTALLER.zip OUTPUT_DIRECTORY" >&2
  exit 2
fi

installer=$(realpath "$1")
output=$(realpath -m "$2")

if [[ -e "$output" ]]; then
  echo "output path already exists: $output" >&2
  exit 1
fi

mkdir -p "$output"

podman run --rm --network=none \
  --volume "$installer:/input/installer.zip:ro" \
  --volume "$output:/output:rw" \
  localhost/rx3-reverse:latest sh -eu -c '
    mkdir -p /output/zip /output/pkg /output/payload
    sha256sum /input/installer.zip > /output/installer.sha256
    unzip -j /input/installer.zip -d /output/zip
    package=$(find /output/zip -maxdepth 1 -type f -name "*.pkg" -print -quit)
    7z x -y -o/output/pkg "$package" >/output/xar-extract.log
    gzip -dc /output/pkg/rekordbox.pkg/Payload |
      (cd /output/payload && cpio -idm --quiet)
    find /output/payload -type f -print | sort > /output/payload-files.txt
  '
