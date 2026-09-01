"""Build a real Tamriel map atlas from Skyrim SE's own terrain LOD textures.

The script reads only the BSA directory and matching LOD records over ADB. It
does not pull the multi-gigabyte texture archive and never launches the game.
Skyrim SE BSA v105 stores compressed payloads as LZ4 frames.
"""

from __future__ import annotations

import argparse
import io
import re
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

import lz4.frame
from PIL import Image


@dataclass(frozen=True)
class Record:
    path: str
    size_flags: int
    offset: int


def adb_bytes(adb: str, device: str, command: str) -> bytes:
    return subprocess.check_output(
        [adb, "-s", device, "exec-out", "sh", "-c", command],
        stderr=subprocess.DEVNULL,
    )


def remote_slice(adb: str, device: str, path: str, offset: int, size: int) -> bytes:
    command = (
        f"dd if='{path}' bs=1 skip={offset} count={size} 2>/dev/null"
    )
    data = adb_bytes(adb, device, command)
    if len(data) != size:
        raise RuntimeError(
            f"short ADB read at {offset}: expected {size}, received {len(data)}"
        )
    return data


def read_records(adb: str, device: str, archive: str) -> tuple[int, list[Record]]:
    header = remote_slice(adb, device, archive, 0, 36)
    magic, version, _, flags, folder_count, file_count, folder_name_bytes, \
        file_name_bytes, _ = struct.unpack("<4s8I", header)
    if magic != b"BSA\0" or version != 105:
        raise RuntimeError(f"unsupported BSA header: magic={magic!r} version={version}")

    # v105 folder records are 24 bytes. folder_name_bytes excludes the one-byte
    # length prefix carried by every folder block.
    metadata_size = (
        36
        + folder_count * 24
        + folder_name_bytes
        + folder_count
        + file_count * 16
        + file_name_bytes
    )
    metadata = remote_slice(adb, device, archive, 0, metadata_size)
    folders: list[tuple[str, list[tuple[int, int, int]]]] = []
    filename_table = 0
    for index in range(folder_count):
        _, count, _, stored_offset = struct.unpack_from(
            "<QIIQ", metadata, 36 + index * 24
        )
        block = stored_offset - file_name_bytes
        name_length = metadata[block]
        name = metadata[block + 1 : block + 1 + name_length]
        folder = name.rstrip(b"\0").decode("utf-8", "replace")
        record_start = block + 1 + name_length
        records = [
            struct.unpack_from("<QII", metadata, record_start + item * 16)
            for item in range(count)
        ]
        folders.append((folder, records))
        filename_table = max(filename_table, record_start + count * 16)

    names: list[str] = []
    cursor = filename_table
    for _ in range(file_count):
        end = metadata.index(0, cursor)
        names.append(metadata[cursor:end].decode("utf-8", "replace"))
        cursor = end + 1

    result: list[Record] = []
    name_index = 0
    for folder, records in folders:
        for _, size_flags, offset in records:
            result.append(Record(folder + "\\" + names[name_index], size_flags, offset))
            name_index += 1
    return flags, result


def extract_record(
    adb: str, device: str, archive: str, archive_flags: int, record: Record
) -> bytes:
    stored_size = record.size_flags & 0x3FFFFFFF
    payload = remote_slice(adb, device, archive, record.offset, stored_size)
    if archive_flags & 0x100:  # embedded full file name
        name_length = payload[0]
        payload = payload[1 + name_length :]

    compressed = bool(archive_flags & 0x4) ^ bool(record.size_flags & 0x40000000)
    if not compressed:
        return payload
    if len(payload) < 8:
        raise RuntimeError(f"compressed record is truncated: {record.path}")
    expected = struct.unpack_from("<I", payload, 0)[0]
    decoded = lz4.frame.decompress(payload[4:])
    if len(decoded) != expected:
        raise RuntimeError(
            f"bad decompressed size for {record.path}: {len(decoded)} != {expected}"
        )
    return decoded


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--archive", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--lod", type=int, default=16)
    parser.add_argument("--size", type=int, default=2048)
    args = parser.parse_args()

    archive_flags, records = read_records(args.adb, args.device, args.archive)
    pattern = re.compile(
        rf"(?:^|\\)tamriel\.{args.lod}\.(-?\d+)\.(-?\d+)\.dds$",
        re.IGNORECASE,
    )
    tiles: dict[tuple[int, int], Record] = {}
    for record in records:
        match = pattern.search(record.path)
        if match:
            tiles[(int(match.group(1)), int(match.group(2)))] = record
    if not tiles:
        raise RuntimeError(f"no Tamriel LOD {args.lod} diffuse tiles were found")

    x_values = sorted({coordinate[0] for coordinate in tiles})
    y_values = sorted({coordinate[1] for coordinate in tiles}, reverse=True)
    tile_size = 256
    atlas = Image.new("RGB", (len(x_values) * tile_size, len(y_values) * tile_size))
    for row, y in enumerate(y_values):
        for column, x in enumerate(x_values):
            record = tiles.get((x, y))
            if record is None:
                continue
            raw = extract_record(
                args.adb, args.device, args.archive, archive_flags, record
            )
            with Image.open(io.BytesIO(raw)) as source:
                tile = source.convert("RGB")
                if tile.size != (tile_size, tile_size):
                    tile = tile.resize((tile_size, tile_size), Image.Resampling.BILINEAR)
                atlas.paste(tile, (column * tile_size, row * tile_size))
        print(f"assembled terrain row {row + 1}/{len(y_values)}", flush=True)

    if atlas.size != (args.size, args.size):
        atlas = atlas.resize((args.size, args.size), Image.Resampling.BOX)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(output, format="PNG", optimize=True)
    print(f"wrote {output} ({atlas.width}x{atlas.height})")


if __name__ == "__main__":
    main()
