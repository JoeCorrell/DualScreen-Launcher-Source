#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_ARM, Cs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("start", type=lambda value: int(value, 0))
    parser.add_argument("end", type=lambda value: int(value, 0))
    parser.add_argument("--base", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()

    if args.start < args.base or args.end <= args.start:
        raise RuntimeError("Invalid virtual address range")
    data = args.image.read_bytes()
    file_start = args.start - args.base
    file_end = args.end - args.base
    if file_start < 0 or file_end > len(data):
        raise RuntimeError("Requested range is outside the image")
    disassembler = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    for instruction in disassembler.disasm(data[file_start:file_end], args.start):
        raw = instruction.bytes.hex()
        print(
            f"{instruction.address:08x}: {raw:<10} "
            f"{instruction.mnemonic:<8} {instruction.op_str}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
