#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path, PurePosixPath


def read_c_string(data: bytes, offset: int) -> str:
    end = data.find(b"\0", offset)
    if end < 0:
        raise RuntimeError("Unterminated SARC filename")
    return data[offset:end].decode("utf-8")


def safe_destination(root: Path, name: str) -> Path:
    parts = PurePosixPath(name).parts
    if not parts or any(part in {"", ".", ".."} for part in parts):
        raise RuntimeError(f"Unsafe SARC filename: {name!r}")
    return root.joinpath(*parts)


def extract(source: Path, output: Path) -> int:
    data = source.read_bytes()
    if data[:4] != b"SARC" or len(data) < 0x20:
        raise RuntimeError(f"Not a SARC archive: {source}")
    byte_order = data[6:8]
    if byte_order == b"\xff\xfe":
        endian = "<"
    elif byte_order == b"\xfe\xff":
        endian = ">"
    else:
        raise RuntimeError(f"Invalid SARC byte-order marker: {byte_order.hex()}")

    header_size = struct.unpack_from(f"{endian}H", data, 4)[0]
    data_offset = struct.unpack_from(f"{endian}I", data, 0x0C)[0]
    if data[header_size : header_size + 4] != b"SFAT":
        raise RuntimeError("SARC has no SFAT table")
    sfat_header_size, entry_count = struct.unpack_from(
        f"{endian}HH", data, header_size + 4
    )
    entries = header_size + sfat_header_size
    sfnt = entries + entry_count * 0x10
    if data[sfnt : sfnt + 4] != b"SFNT":
        raise RuntimeError("SARC has no SFNT table")
    sfnt_header_size = struct.unpack_from(f"{endian}H", data, sfnt + 4)[0]
    names = sfnt + sfnt_header_size

    output.mkdir(parents=True, exist_ok=True)
    for index in range(entry_count):
        entry = entries + index * 0x10
        _, name_attributes, start, end = struct.unpack_from(
            f"{endian}IIII", data, entry
        )
        if name_attributes >> 24:
            name_offset = (name_attributes & 0x00FFFFFF) * 4
            name = read_c_string(data, names + name_offset)
        else:
            name = f"{index:04d}.bin"
        absolute_start = data_offset + start
        absolute_end = data_offset + end
        if absolute_start > absolute_end or absolute_end > len(data):
            raise RuntimeError(f"Invalid SARC range for {name}")
        destination = safe_destination(output, name)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data[absolute_start:absolute_end])
        print(f"Extracted {name} ({absolute_end - absolute_start:,} bytes)")
    return entry_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    count = extract(args.input, args.output)
    print(f"Extracted {count} SARC files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
