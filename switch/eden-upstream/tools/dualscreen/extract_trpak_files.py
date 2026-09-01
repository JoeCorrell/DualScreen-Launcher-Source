#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path


def table_field(data: bytes, table: int, index: int) -> int | None:
    vtable = table - struct.unpack_from("<i", data, table)[0]
    vtable_size = struct.unpack_from("<H", data, vtable)[0]
    entry = vtable + 4 + index * 2
    if entry + 2 > vtable + vtable_size:
        return None
    offset = struct.unpack_from("<H", data, entry)[0]
    return table + offset if offset else None


def vector(data: bytes, table: int, index: int) -> tuple[int, int]:
    field = table_field(data, table, index)
    if field is None:
        return 0, 0
    target = field + struct.unpack_from("<I", data, field)[0]
    return target + 4, struct.unpack_from("<I", data, target)[0]


def guess_extension(data: bytes) -> str:
    if data.startswith(b"AHTB"):
        return "ahtb"
    if data.startswith(b"BNTX"):
        return "bntx"
    if len(data) >= 16 and data[:2] == b"\x01\0" and data[8:16] == b"\0\0\0\0\x10\0\0\0":
        return "dat"
    return "bin"


def decompress_oodle(ooz: Path, packed: bytes, size: int) -> bytes:
    with tempfile.TemporaryDirectory(prefix="eden-trpak-") as temporary:
        source = Path(temporary) / "source.ooz"
        destination = Path(temporary) / "result.bin"
        source.write_bytes(struct.pack("<Q", size) + packed)
        subprocess.run(
            [str(ooz), "-f", str(source), str(destination)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        result = destination.read_bytes()
    if len(result) != size:
        raise RuntimeError(f"Oodle output size mismatch: expected {size}, got {len(result)}")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ooz", type=Path, required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    root = struct.unpack_from("<I", data)[0]
    hashes_start, hash_count = vector(data, root, 0)
    files_start, file_count = vector(data, root, 1)
    if hash_count != file_count or not file_count:
        raise RuntimeError("Invalid Trinity package file/hash vectors")
    hashes = struct.unpack_from(f"<{hash_count}Q", data, hashes_start)
    args.output.mkdir(parents=True, exist_ok=True)

    for index, file_hash in enumerate(hashes):
        element = files_start + index * 4
        table = element + struct.unpack_from("<I", data, element)[0]
        compression_field = table_field(data, table, 1)
        size_field = table_field(data, table, 3)
        payload_start, payload_size = vector(data, table, 4)
        if compression_field is None or size_field is None or not payload_size:
            raise RuntimeError(f"Invalid Trinity package file entry {index}")
        compression = data[compression_field]
        unpacked_size = struct.unpack_from("<Q", data, size_field)[0]
        payload = data[payload_start : payload_start + payload_size]
        if compression == 0xFF:
            unpacked = payload
        elif compression == 1:
            unpacked = zlib.decompress(payload)
        elif 3 <= compression <= 7:
            unpacked = decompress_oodle(args.ooz, payload, unpacked_size)
        else:
            raise RuntimeError(f"Unsupported Trinity compression {compression} in file {index}")
        extension = guess_extension(unpacked)
        destination = args.output / f"{index:04d}-{file_hash:016X}.{extension}"
        destination.write_bytes(unpacked)
        print(f"Extracted {destination.name} ({len(unpacked):,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
