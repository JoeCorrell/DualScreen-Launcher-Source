#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        return ""
    return data[offset:end].decode("utf-8", "replace")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("--match", action="append", default=[])
    args = parser.parse_args()

    data = args.image.read_bytes()
    module = struct.unpack_from("<I", data, 4)[0]
    if data[module : module + 4] != b"MOD0":
        raise RuntimeError("MOD0 header was not found")
    dynamic = module + struct.unpack_from("<i", data, module + 4)[0]
    tags: dict[int, list[int]] = {}
    for offset in range(dynamic, len(data), 16):
        tag, value = struct.unpack_from("<qQ", data, offset)
        if tag == 0:
            break
        tags.setdefault(tag, []).append(value)

    symbol_table = tags[6][0]
    string_table = tags[5][0]
    if 4 in tags:
        _, symbol_count = struct.unpack_from("<II", data, tags[4][0])
    else:
        symbol_count = (string_table - symbol_table) // 24
    symbols: list[tuple[str, int, int, int, int]] = []
    for index in range(symbol_count):
        offset = symbol_table + index * 24
        name_offset, info, other, section, value, size = struct.unpack_from(
            "<IBBHQQ", data, offset
        )
        name = c_string(data, string_table + name_offset) if name_offset else ""
        symbols.append((name, info, section, value, size))

    needles = [value.casefold() for value in args.match]
    print(
        f"MOD0=0x{module:X} DYNAMIC=0x{dynamic:X} "
        f"DYNSYM=0x{symbol_table:X} SYMBOLS={len(symbols)}"
    )
    for index, (name, info, section, value, size) in enumerate(symbols):
        if name and (not needles or any(needle in name.casefold() for needle in needles)):
            print(
                f"symbol[{index}] section={section} info=0x{info:02X} "
                f"value=0x{value:X} size=0x{size:X} {name}"
            )

    matching_symbols = {
        index
        for index, (name, _, _, _, _) in enumerate(symbols)
        if name and (not needles or any(needle in name.casefold() for needle in needles))
    }
    for relocation_tag, size_tag, label in ((7, 8, "RELA"), (23, 2, "JMPREL")):
        if relocation_tag not in tags or size_tag not in tags:
            continue
        start = tags[relocation_tag][0]
        size = tags[size_tag][0]
        for offset in range(start, start + size, 24):
            destination, info, addend = struct.unpack_from("<QQq", data, offset)
            symbol_index = info >> 32
            if symbol_index not in matching_symbols:
                continue
            name = symbols[symbol_index][0]
            print(
                f"{label}@0x{offset:X} destination=0x{destination:X} "
                f"type={info & 0xFFFFFFFF} addend=0x{addend:X} symbol={name}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
