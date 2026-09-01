#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


def read_u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise RuntimeError("Invalid Trinity descriptor offset")
    return struct.unpack_from("<I", data, offset)[0]


def parse_names(data: bytes) -> list[str]:
    root = read_u32(data, 0)
    if root < 4 or root + 4 > len(data):
        raise RuntimeError("Invalid Trinity descriptor root")
    vtable_distance = read_u32(data, root)
    if vtable_distance > root:
        raise RuntimeError("Invalid Trinity descriptor vtable")
    vtable = root - vtable_distance
    if vtable + 8 > len(data):
        raise RuntimeError("Invalid Trinity descriptor vtable")
    vtable_size = struct.unpack_from("<H", data, vtable)[0]
    if vtable_size < 8 or vtable + vtable_size > len(data):
        raise RuntimeError("Invalid Trinity descriptor vtable")
    names_offset = struct.unpack_from("<H", data, vtable + 6)[0]
    if names_offset == 0 or root + names_offset + 4 > len(data):
        raise RuntimeError("Missing Trinity descriptor names")
    vector_pointer = root + names_offset
    table = vector_pointer + read_u32(data, vector_pointer)
    count = read_u32(data, table)
    if count > 1_000_000 or table + 4 + count * 4 > len(data):
        raise RuntimeError("Invalid Trinity descriptor table")
    result: list[str] = []
    for index in range(count):
        relative = read_u32(data, table + 4 + index * 4)
        entry = table + 4 + index * 4 + relative
        length = read_u32(data, entry)
        start = entry + 4
        if length > len(data) - start:
            raise RuntimeError(f"Invalid Trinity descriptor string at index {index}")
        result.append(data[start : start + length].decode("utf-8", "replace"))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("descriptor", type=Path)
    parser.add_argument("--match", default=".", help="case-insensitive regular expression")
    args = parser.parse_args()

    pattern = re.compile(args.match, re.IGNORECASE)
    names = parse_names(args.descriptor.read_bytes())
    selected = [name for name in names if pattern.search(name)]
    for name in selected:
        print(name)
    print(f"Matched {len(selected)} of {len(names)} Trinity packages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
