#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
DISK_SIZE = 5 * 1024 * 1024


def main() -> int:
    disk_path = ROOT / "build/rx3disk.bin"
    payload_path = ROOT / "msc-root/autoexec.bin"
    dropbear_path = ROOT / "msc-root/RX3_SSH/dropbearmulti"
    authorized_keys_path = ROOT / "msc-root/RX3_SSH/authorized_keys"

    disk = disk_path.read_bytes()
    payload = payload_path.read_bytes()
    dropbear = dropbear_path.read_bytes()
    authorized_keys = authorized_keys_path.read_bytes()

    if len(disk) != DISK_SIZE:
        raise AssertionError(f"disk image is {len(disk)} bytes, expected {DISK_SIZE}")
    if not payload:
        raise AssertionError("staged autoexec.bin is empty")
    if disk.find(payload) < 0:
        raise AssertionError("generated FAT image does not contain staged autoexec.bin")
    if disk.find(dropbear) < 0:
        raise AssertionError("generated FAT image does not contain staged dropbearmulti")
    if disk.find(authorized_keys) < 0:
        raise AssertionError("generated FAT image does not contain authorized_keys")
    print(
        "disk image validation passed "
        f"({len(payload)}-byte autoexec.bin, {len(dropbear)}-byte dropbearmulti)"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, FileNotFoundError) as error:
        print(f"disk image validation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
