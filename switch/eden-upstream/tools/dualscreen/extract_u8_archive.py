#!/usr/bin/env python3
"""List or extract Nintendo U8 archives used by Skyward Sword HD.

The format stores a flat, big-endian node table. Directory nodes point at the
first node after their subtree, which lets this parser validate nesting without
depending on filename extensions.
"""

from __future__ import annotations

import argparse
import fnmatch
import struct
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


U8_MAGIC = 0x55AA382D


@dataclass(frozen=True)
class Entry:
    path: PurePosixPath
    offset: int
    size: int


def read_c_string(data: bytes, offset: int, limit: int) -> str:
    if offset < 0 or offset >= limit:
        raise ValueError(f"string offset 0x{offset:x} is outside the archive")
    end = data.find(b"\0", offset, limit)
    if end < 0:
        raise ValueError(f"unterminated string at 0x{offset:x}")
    return data[offset:end].decode("utf-8", errors="strict")


def parse_u8(data: bytes) -> list[Entry]:
    if len(data) < 0x20:
        raise ValueError("archive is smaller than a U8 header")
    magic, root_offset, header_size, data_offset = struct.unpack_from(">4I", data, 0)
    if magic != U8_MAGIC:
        raise ValueError(f"unexpected U8 magic 0x{magic:08x}")
    if root_offset + 12 > len(data):
        raise ValueError("root node is outside the archive")

    root_type_name, root_parent, node_count = struct.unpack_from(">3I", data, root_offset)
    if root_type_name >> 24 != 1 or root_parent != 0 or node_count == 0:
        raise ValueError("invalid U8 root node")
    nodes_end = root_offset + node_count * 12
    strings_offset = nodes_end
    header_end = root_offset + header_size
    if nodes_end > len(data) or header_end > len(data) or data_offset > len(data):
        raise ValueError("U8 header points outside the archive")
    if not (nodes_end <= header_end <= data_offset):
        raise ValueError("U8 node/string/data regions overlap")

    entries: list[Entry] = []
    # Stack entries are (exclusive subtree end, directory path).
    stack: list[tuple[int, PurePosixPath]] = [(node_count, PurePosixPath())]
    for index in range(1, node_count):
        while stack and index >= stack[-1][0]:
            stack.pop()
        if not stack:
            raise ValueError(f"node {index} is outside the root subtree")

        type_name, value, size_or_end = struct.unpack_from(">3I", data, root_offset + index * 12)
        node_type = type_name >> 24
        name_offset = type_name & 0x00FFFFFF
        name = read_c_string(data, strings_offset + name_offset, header_end)
        if not name or name in {".", ".."} or "/" in name or "\\" in name:
            raise ValueError(f"unsafe node name {name!r} at index {index}")
        path = stack[-1][1] / name

        if node_type == 1:
            if size_or_end <= index or size_or_end > node_count:
                raise ValueError(f"invalid subtree end {size_or_end} at node {index}")
            stack.append((size_or_end, path))
        elif node_type == 0:
            end = value + size_or_end
            if value < data_offset or end < value or end > len(data):
                raise ValueError(f"file {path} points outside the archive")
            entries.append(Entry(path, value, size_or_end))
        else:
            raise ValueError(f"unsupported node type {node_type} at index {index}")
    return entries


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--match", default="*", help="case-insensitive glob over archive paths")
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    data = args.archive.read_bytes()
    entries = parse_u8(data)
    pattern = args.match.casefold()
    selected = [entry for entry in entries if fnmatch.fnmatch(entry.path.as_posix().casefold(), pattern)]
    for entry in selected:
        print(f"{entry.path.as_posix()}\t{entry.size}")

    if args.extract:
        if args.output is None:
            parser.error("--extract requires --output")
        output_root = args.output.resolve()
        for entry in selected:
            target = output_root.joinpath(*entry.path.parts).resolve()
            if output_root not in target.parents:
                raise ValueError(f"unsafe extraction target: {target}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data[entry.offset : entry.offset + entry.size])

    print(f"Matched {len(selected)} of {len(entries)} files")


if __name__ == "__main__":
    main()
