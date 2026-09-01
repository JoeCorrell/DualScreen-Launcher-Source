#!/usr/bin/env python3



from __future__ import annotations

import argparse
import struct
from pathlib import Path

import lz4.block


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    source = args.input.read_bytes()
    if len(source) < 0x100 or source[:4] != b"NSO0":
        raise RuntimeError("Input is not an NSO")
    flags = struct.unpack_from("<I", source, 0xC)[0]
    segments: list[tuple[int, int, int, int]] = []
    for index in range(3):
        header_offset = 0x10 + index * 0x10
        file_offset, location, size = struct.unpack_from("<III", source, header_offset)
        compressed_size = struct.unpack_from("<I", source, 0x60 + index * 4)[0]
        segments.append((file_offset, location, size, compressed_size))
    image_size = max(location + size for _, location, size, _ in segments)
    image = bytearray(image_size)
    for index, (file_offset, location, size, compressed_size) in enumerate(segments):
        stored_size = compressed_size if flags & (1 << index) else size
        stored = source[file_offset : file_offset + stored_size]
        if len(stored) != stored_size:
            raise RuntimeError(f"NSO segment {index} is truncated")
        plain = lz4.block.decompress(stored, uncompressed_size=size) if flags & (1 << index) else stored
        if len(plain) != size:
            raise RuntimeError(f"NSO segment {index} has the wrong decompressed size")
        image[location : location + size] = plain
        print(f"Segment {index}: address=0x{location:x} size=0x{size:x}")
    print(f"Build ID: {source[0x40:0x48].hex().upper()}")
    args.output.write_bytes(image)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
