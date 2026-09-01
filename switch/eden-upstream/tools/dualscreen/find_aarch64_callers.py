#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def sign_extend(value: int, bits: int) -> int:
    sign = 1 << (bits - 1)
    return (value ^ sign) - sign


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("target", type=lambda value: int(value, 0))
    parser.add_argument("--text-size", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()

    data = args.image.read_bytes()
    size = min(args.text_size, len(data)) & ~3
    found = 0
    for offset in range(0, size, 4):
        instruction = struct.unpack_from("<I", data, offset)[0]
        if instruction & 0xFC000000 != 0x94000000:
            continue
        pc = args.base + offset
        immediate = sign_extend(instruction & 0x03FFFFFF, 26) << 2
        if pc + immediate == args.target:
            print(f"0x{pc:08X}")
            found += 1
    print(f"Found {found} callers")
    return 0 if found else 1


if __name__ == "__main__":
    raise SystemExit(main())
