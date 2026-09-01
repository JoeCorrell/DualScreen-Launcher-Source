#!/usr/bin/env python3
"""Decompress Nintendo LZ10/LZ11 streams with strict bounds checks."""

from __future__ import annotations

import argparse
from pathlib import Path


def decompress(data: bytes) -> bytes:
    if len(data) < 4 or data[0] not in {0x10, 0x11}:
        raise ValueError("expected a Nintendo LZ10 or LZ11 stream")
    kind = data[0]
    output_size = int.from_bytes(data[1:4], "little")
    cursor = 4
    if output_size == 0:
        if len(data) < 8:
            raise ValueError("truncated extended-size header")
        output_size = int.from_bytes(data[4:8], "little")
        cursor = 8
    if output_size <= 0:
        raise ValueError("invalid decompressed size")

    output = bytearray()
    while len(output) < output_size:
        if cursor >= len(data):
            raise ValueError("truncated flag byte")
        flags = data[cursor]
        cursor += 1
        for bit in range(7, -1, -1):
            if len(output) >= output_size:
                break
            if not (flags & (1 << bit)):
                if cursor >= len(data):
                    raise ValueError("truncated literal")
                output.append(data[cursor])
                cursor += 1
                continue

            if cursor + 2 > len(data):
                raise ValueError("truncated back-reference")
            first = data[cursor]
            high = first >> 4
            if kind == 0x10:
                length = high + 3
                distance = (((first & 0xF) << 8) | data[cursor + 1]) + 1
                cursor += 2
            elif high == 0:
                if cursor + 3 > len(data):
                    raise ValueError("truncated LZ11 medium back-reference")
                length = (((first & 0xF) << 4) | (data[cursor + 1] >> 4)) + 0x11
                distance = (((data[cursor + 1] & 0xF) << 8) | data[cursor + 2]) + 1
                cursor += 3
            elif high == 1:
                if cursor + 4 > len(data):
                    raise ValueError("truncated LZ11 long back-reference")
                length = (
                    ((first & 0xF) << 12)
                    | (data[cursor + 1] << 4)
                    | (data[cursor + 2] >> 4)
                ) + 0x111
                distance = (((data[cursor + 2] & 0xF) << 8) | data[cursor + 3]) + 1
                cursor += 4
            else:
                length = high + 1
                distance = (((first & 0xF) << 8) | data[cursor + 1]) + 1
                cursor += 2

            if distance > len(output):
                raise ValueError(f"invalid back-reference distance {distance}")
            for _ in range(min(length, output_size - len(output))):
                output.append(output[-distance])
    return bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    decoded = decompress(args.input.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(decoded)
    print(f"Decoded {args.input} -> {args.output} ({len(decoded)} bytes)")


if __name__ == "__main__":
    main()
