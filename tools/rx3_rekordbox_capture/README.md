<!-- SPDX-License-Identifier: MPL-2.0 -->
# Rekordbox capture device build

`build-device.sh OUTPUT_DIRECTORY` compiles the firmware 1.19 passive preload and
the freestanding `eth0` PCAP recorder inside the offline
`localhost/rx3-reverse:latest` rootless Podman image. The repository is mounted
read-only and only the requested output directory is writable.
