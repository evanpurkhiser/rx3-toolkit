#!/usr/bin/env python3
"""Inspect the RX3 firmware's live SOURCE-page model over diagnostic Telnet."""

from __future__ import annotations

import argparse
import re
import socket
import struct
import time


MARKER = "RX3UI_"
SLOT_COUNT = 4
SLOT_SIZE = 0x48
ROW_COUNT = 4
ROW_SIZE = 0x238
ROW_NAME_SIZE = 66


def receive_available(connection: socket.socket) -> bytes:
    received = bytearray()
    while True:
        try:
            chunk = connection.recv(65535)
        except TimeoutError:
            return bytes(received)
        if not chunk:
            return bytes(received)
        received.extend(chunk)


def decode_name(data: bytes) -> str:
    terminator = next(
        (offset for offset in range(0, len(data) - 1, 2) if data[offset : offset + 2] == b"\0\0"),
        len(data),
    )
    return data[:terminator].decode("utf-16le", "replace")


def field(output: str, name: str) -> str:
    match = re.search(rf"{MARKER}{name}=([^\r\n]*)", output)
    if not match:
        raise RuntimeError(f"RX3 response did not contain {name}")
    return match.group(1).strip()


def inspect(host: str, port: int) -> None:
    command = r'''pid=; for process in /proc/[0-9]*; do [ "$(cat "$process/comm" 2>/dev/null)" = rbp ] || continue; grep -q '^State:.*Z' "$process/status" 2>/dev/null && continue; pid=${process#/proc/}; done; read32() { dd if=/proc/$pid/mem bs=1 skip=$1 count=4 2>/dev/null | hexdump -v -e '1/4 "%u"'; }; readhex() { dd if=/proc/$pid/mem bs=1 skip=$1 count=$2 2>/dev/null | hexdump -v -e '1/1 "%02x"'; }; net=$(read32 $((0x02685fdc))); echo RX3UI_MODE=$(read32 $((0x0326f8b8))); echo RX3UI_DEVICE=$(read32 $((0x0326f8bc))); echo RX3UI_MEDIA=$(read32 $((0x0326f8b4))); echo RX3UI_PC_CONNECT=$(read32 $((0x0326f904))); echo RX3UI_CERTIFIED=$(readhex $((0x026870d0)) 1); echo RX3UI_DETECT11=$(read32 $((0x0325b818))); echo RX3UI_DETECT12=$(read32 $((0x0325bcb0))); echo RX3UI_DETECT29=$(read32 $((0x03262658))); echo RX3UI_DETECT2A=$(read32 $((0x03262af0))); echo RX3UI_DETECT2B=$(read32 $((0x03262f88))); echo RX3UI_DETECT2C=$(read32 $((0x03263420))); echo RX3UI_DRIVES=$(readhex $((0x05a13f10)) $((0x410))); echo RX3UI_ROWS=$(read32 $((0x0551aba0))); echo RX3UI_WINDOW=$(read32 $((0x0268543c))); echo RX3UI_SLOTS=$(readhex $((net+0x74)) $((4*0x48))); echo RX3UI_ROW0=$(readhex $((0x0551abca+0*0x238)) 66); echo RX3UI_ROW1=$(readhex $((0x0551abca+1*0x238)) 66); echo RX3UI_ROW2=$(readhex $((0x0551abca+2*0x238)) 66); echo RX3UI_ROW3=$(readhex $((0x0551abca+3*0x238)) 66)'''

    with socket.create_connection((host, port), timeout=5) as connection:
        connection.settimeout(0.4)
        time.sleep(0.4)
        receive_available(connection)
        connection.sendall(b"stty -echo\r\n")
        time.sleep(0.2)
        receive_available(connection)
        connection.sendall((command + "\r\n").encode())
        time.sleep(1.0)
        output = receive_available(connection).decode("latin1", "replace")

    mode = int(field(output, "MODE"))
    device = int(field(output, "DEVICE"))
    media = int(field(output, "MEDIA"))
    pc_connect = int(field(output, "PC_CONNECT"))
    certified = int(field(output, "CERTIFIED"), 16)
    detect_ids = ("11", "12", "29", "2A", "2B", "2C")
    detect = [int(field(output, f"DETECT{device}")) for device in detect_ids]
    drives = bytes.fromhex(field(output, "DRIVES"))
    row_count = int(field(output, "ROWS"))
    window = int(field(output, "WINDOW"))
    slots = bytes.fromhex(field(output, "SLOTS"))
    row_names = [
        decode_name(bytes.fromhex(field(output, f"ROW{index}")))
        for index in range(ROW_COUNT)
    ]

    print(f"browse_mode={mode} source_active={mode == 12}")
    print(
        f"browse_device={device} connected_media=0x{media:02x} "
        f"pc_connect={pc_connect} certified={certified} "
        f"pc_detect={dict(zip(detect_ids, detect))}"
    )
    nfs_drives = []
    for index in range(0, len(drives), 0x28):
        flags, kind, pointer = struct.unpack_from("<III", drives, index)
        if kind == 5:
            nfs_drives.append(
                f"{chr(0x41 + index // 0x28)}(flags=0x{flags:x},ptr=0x{pointer:x})"
            )
    print(f"nfs_pc_drives={nfs_drives}")
    print(f"source_window={window} source_rows={row_count}")
    for index in range(SLOT_COUNT):
        slot = slots[index * SLOT_SIZE : (index + 1) * SLOT_SIZE]
        link_id = struct.unpack_from("<I", slot)[0]
        if link_id:
            print(
                f"peer[{index}] id={link_id} flags=0x{slot[0x46]:02x} "
                f"name={decode_name(slot[4:0x46])!r}"
            )
    for index, name in enumerate(row_names[:row_count]):
        print(f"row[{index}] name={name!r}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="169.254.100.2")
    parser.add_argument("--port", type=int, default=23)
    arguments = parser.parse_args()
    inspect(arguments.host, arguments.port)


if __name__ == "__main__":
    main()
