#!/usr/bin/env python3
"""Print one file from a Bethesda BSA v104 archive to stdout."""

import argparse
import re
import struct
import sys
import zlib


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def read_c_string(data, offset):
    end = data.index(0, offset)
    return data[offset:end].decode("cp1252"), end + 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("entry", nargs="?")
    parser.add_argument(
        "--list",
        action="store_true",
        help="list archive paths, optionally filtered by the entry argument",
    )
    parser.add_argument(
        "--metadata-only",
        action="store_true",
        help="print offset, stored size and compression without reading payload",
    )
    parser.add_argument(
        "--strings",
        action="store_true",
        help="print printable strings from the decompressed payload",
    )
    parser.add_argument(
        "--dds-info",
        action="store_true",
        help="print the DDS dimensions and compression format",
    )
    args = parser.parse_args()

    data = open(args.archive, "rb").read()
    if len(data) < 36 or data[:4] != b"BSA\0" or u32(data, 4) != 104:
        raise SystemExit("not a Fallout 3 BSA v104 archive")

    folder_offset = u32(data, 8)
    flags = u32(data, 12)
    folder_count = u32(data, 16)
    file_count = u32(data, 20)
    if not flags & 1 or not flags & 2:
        raise SystemExit("archive omits directory or file names")

    folder_records = []
    cursor = folder_offset
    for _ in range(folder_count):
        _, count, offset = struct.unpack_from("<QII", data, cursor)
        folder_records.append((count, offset))
        cursor += 16

    records = []
    cursor = folder_offset + folder_count * 16
    for count, _ in folder_records:
        name_length = data[cursor]
        cursor += 1
        folder = data[cursor:cursor + name_length].rstrip(b"\0").decode("cp1252")
        cursor += name_length
        for _ in range(count):
            _, size_flags, data_offset = struct.unpack_from("<QII", data, cursor)
            records.append([folder, "", size_flags, data_offset])
            cursor += 16

    if len(records) != file_count:
        raise SystemExit("BSA directory count mismatch")
    for record in records:
        record[1], cursor = read_c_string(data, cursor)

    if args.list:
        needle = (args.entry or "").replace("/", "\\").lower().strip("\\")
        for folder, filename, _, _ in records:
            path = (folder + "\\" + filename).strip("\\")
            if needle in path.lower():
                print(path)
        return
    if not args.entry:
        raise SystemExit("entry is required unless --list is used")
    wanted = args.entry.replace("/", "\\").lower().strip("\\")
    for folder, filename, size_flags, data_offset in records:
        path = (folder + "\\" + filename).lower().strip("\\")
        if path != wanted:
            continue
        size = size_flags & 0x3FFFFFFF
        compressed = bool(flags & 4) != bool(size_flags & 0x40000000)
        if args.metadata_only:
            print(
                f"offset={data_offset} size={size} "
                f"compressed={int(compressed)} embedded={int(bool(flags & 0x100))}"
            )
            return
        payload = data[data_offset:data_offset + size]
        if flags & 0x100:
            embedded_length = payload[0]
            payload = payload[1 + embedded_length:]
        if compressed:
            expected = u32(payload, 0)
            payload = zlib.decompress(payload[4:])
            if len(payload) != expected:
                raise SystemExit("decompressed size mismatch")
        if args.dds_info:
            if len(payload) < 128 or payload[:4] != b"DDS ":
                raise SystemExit("entry is not a DDS file")
            height = u32(payload, 12)
            width = u32(payload, 16)
            fourcc = payload[84:88].decode("ascii", "replace").rstrip("\0")
            print(f"width={width} height={height} format={fourcc or 'raw'}")
            return
        if args.strings:
            for match in re.finditer(rb"[ -~]{4,}", payload):
                print(match.group().decode("ascii", "replace"))
            return
        sys.stdout.buffer.write(payload)
        return
    raise SystemExit("entry not found: " + args.entry)


if __name__ == "__main__":
    main()
