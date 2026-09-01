#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def sign_extend(value: int, bits: int) -> int:
    sign_bit = 1 << (bits - 1)
    return (value ^ sign_bit) - sign_bit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("target", type=lambda value: int(value, 0))
    parser.add_argument("--text-size", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--look-ahead", type=int, default=10)
    args = parser.parse_args()

    image = args.image.read_bytes()
    text_size = min(args.text_size, len(image)) & ~3
    found = 0
    for pc in range(0, text_size, 4):
        instruction = struct.unpack_from("<I", image, pc)[0]
        if instruction & 0x9F000000 != 0x90000000:
            continue
        register = instruction & 0x1F
        immediate = ((instruction >> 5) & 0x7FFFF) << 2
        immediate |= (instruction >> 29) & 0x3
        page = (pc & ~0xFFF) + (sign_extend(immediate, 21) << 12)
        for index in range(1, args.look_ahead + 1):
            next_pc = pc + index * 4
            if next_pc >= text_size:
                break
            next_instruction = struct.unpack_from("<I", image, next_pc)[0]
            if next_instruction & 0x7C000000 == 0x14000000:
                break
            source_register = (next_instruction >> 5) & 0x1F
            if source_register != register:
                continue
            address = None
            operation = ""
            if next_instruction & 0xFF000000 == 0x91000000:
                shift = 12 if (next_instruction >> 22) & 1 else 0
                address = page + (((next_instruction >> 10) & 0xFFF) << shift)
                operation = "add"
            elif next_instruction & 0x3B000000 == 0x39000000:
                scale = (next_instruction >> 30) & 0x3
                address = page + (((next_instruction >> 10) & 0xFFF) << scale)
                operation = "load/store"
            if address == args.target:
                print(f"adrp=0x{pc:x} use=0x{next_pc:x} operation={operation}")
                found += 1
    print(f"Found {found} references")
    return 0 if found else 1


if __name__ == "__main__":
    raise SystemExit(main())
