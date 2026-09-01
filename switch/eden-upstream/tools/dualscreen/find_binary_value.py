#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("value", type=lambda value: int(value, 0))
    parser.add_argument("--width", type=int, choices=(4, 8), default=8)
    parser.add_argument("--alignment", type=int, default=1)
    args = parser.parse_args()

    data = args.image.read_bytes()
    encoded = args.value.to_bytes(args.width, "little")
    start = 0
    count = 0
    while True:
        offset = data.find(encoded, start)
        if offset < 0:
            break
        if offset % args.alignment == 0:
            print(f"0x{offset:08X}")
            count += 1
        start = offset + 1
    print(f"Found {count} matches")
    return 0 if count else 1


if __name__ == "__main__":
    raise SystemExit(main())
