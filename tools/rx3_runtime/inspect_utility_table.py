#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Decode the firmware 1.19 static utility-menu descriptor table."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


TABLE_COUNT_ADDRESS = 0x005140B8
TABLE_ADDRESS = 0x005140BC
ENTRY_SIZE = 0x38


class Image:
    def __init__(self, path: Path) -> None:
        self.stream = path.open("rb")
        self.elf = ELFFile(self.stream)
        self.symbols = self._load_symbols()

    def close(self) -> None:
        self.stream.close()

    def _load_symbols(self) -> dict[int, str]:
        section = self.elf.get_section_by_name(".symtab")
        if section is None:
            raise ValueError("rbp has no static symbol table")

        symbols: dict[int, str] = {}
        for symbol in section.iter_symbols():
            if symbol.entry.st_value and symbol.name:
                symbols.setdefault(symbol.entry.st_value, symbol.name)
        return symbols

    def read(self, address: int, size: int) -> bytes:
        for segment in self.elf.iter_segments():
            start = segment.header.p_vaddr
            end = start + segment.header.p_filesz
            if start <= address and address + size <= end:
                offset = segment.header.p_offset + address - start
                self.stream.seek(offset)
                return self.stream.read(size)
        raise ValueError(f"address 0x{address:08x} is outside file-backed segments")

    def u32(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def utf16(self, address: int, limit: int = 256) -> str:
        units = bytearray()
        for offset in range(0, limit * 2, 2):
            unit = self.read(address + offset, 2)
            if unit == b"\0\0":
                return units.decode("utf-16le")
            units.extend(unit)
        raise ValueError(f"unterminated UTF-16 string at 0x{address:08x}")

    def describe_pointer(self, pointer: int) -> str:
        if pointer == 0:
            return "-"
        return self.symbols.get(pointer, f"0x{pointer:08x}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("rbp", type=Path)
    args = parser.parse_args()

    image = Image(args.rbp)
    try:
        count = image.u32(TABLE_COUNT_ADDRESS)
        print(f"utility_items={count} table=0x{TABLE_ADDRESS:08x} entry_size=0x{ENTRY_SIZE:x}")
        print("index\tlabel\tvalues\teditable\tcallbacks")

        for index in range(count):
            address = TABLE_ADDRESS + index * ENTRY_SIZE
            words = struct.unpack("<14I", image.read(address, ENTRY_SIZE))
            label = image.utf16(words[0]) if words[0] else "<separator>"
            option_count = words[2]
            values = []
            if words[1] and option_count:
                for option in range(option_count):
                    pointer = image.u32(words[1] + option * 4)
                    values.append(image.utf16(pointer))

            callbacks = ",".join(
                image.describe_pointer(pointer) for pointer in words[6:14]
            )
            print(
                f"{index}\t{label}\t{'|'.join(values)}\t{words[5]}\t{callbacks}"
            )
    finally:
        image.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
