#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

from extract_botw_exefs import content_key, decrypt_ctr, decrypt_xts, read_keys
from extract_nsp_romfs_files import (
    find_romfs_level,
    parse_pfs0,
    parse_romfs_files,
    parse_romfs_header,
)
from list_trpfd import parse_names


def fnv1a_64(data: bytes) -> int:
    value = 0xCBF29CE484222645
    for byte in data:
        value ^= byte
        value = value * 0x100000001B3 & 0xFFFFFFFFFFFFFFFF
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--local-nsp", type=Path, required=True)
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--title-keys", type=Path, required=True)
    parser.add_argument("--descriptor", type=Path, required=True)
    parser.add_argument("--match", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    patterns = [re.compile(value, re.IGNORECASE) for value in args.match]
    with args.local_nsp.open("rb") as source:
        pfs_prefix = source.read(0x10000)
        pfs_data_offset, pfs_files = parse_pfs0(pfs_prefix)
        candidates = [
            entry
            for entry in pfs_files
            if entry.name.endswith(".nca") and not entry.name.endswith(".cnmt.nca")
        ]
        if not candidates:
            raise RuntimeError("The NSP contains no program content NCA")
        nca = max(candidates, key=lambda entry: entry.size)
        nca_source_offset = pfs_data_offset + nca.offset

        source.seek(nca_source_offset)
        encrypted_header = source.read(0xC00)
        production = read_keys(args.prod_keys)
        titles = read_keys(args.title_keys)
        header = decrypt_xts(encrypted_header, bytes.fromhex(production["header_key"]))
        if header[0x200:0x204] != b"NCA3":
            raise RuntimeError("NCA header decryption failed")

        rights_id = header[0x230:0x240].hex()
        if rights_id != "0" * 32 and rights_id not in titles:
            ticket = next((entry for entry in pfs_files if entry.name.endswith(".tik")), None)
            if ticket is None:
                raise RuntimeError("The NSP contains no ticket for its title key")
            source.seek(pfs_data_offset + ticket.offset)
            ticket_data = source.read(ticket.size)
            if len(ticket_data) < 0x2B0 or ticket_data[0x2A0:0x2B0].hex() != rights_id:
                raise RuntimeError("The NSP ticket does not match its program content")
            titles[rights_id] = ticket_data[0x180:0x190].hex()

        key = content_key(header, production, titles)
        level_offset, level_size, section_ctr = find_romfs_level(header)

        def read_plain(absolute_nca_offset: int, size: int) -> bytes:
            aligned_start = absolute_nca_offset & ~0xF
            aligned_end = absolute_nca_offset + size
            if aligned_start < level_offset or aligned_end > level_offset + level_size:
                raise RuntimeError(
                    "Requested range extends beyond the RomFS data level: "
                    f"0x{aligned_start:x}-0x{aligned_end:x} outside "
                    f"0x{level_offset:x}-0x{level_offset + level_size:x}"
                )
            source.seek(nca_source_offset + aligned_start)
            encrypted = source.read(aligned_end - aligned_start)
            if len(encrypted) != aligned_end - aligned_start:
                raise RuntimeError("The NSP ended during a package range read")
            plain = decrypt_ctr(encrypted, key, section_ctr, aligned_start)
            begin = absolute_nca_offset - aligned_start
            return plain[begin : begin + size]

        romfs_header = parse_romfs_header(read_plain(level_offset, 0x1000))
        metadata_start = min(
            romfs_header.directory_meta_offset, romfs_header.file_meta_offset
        )
        metadata_end = max(
            romfs_header.directory_meta_offset + romfs_header.directory_meta_size,
            romfs_header.file_meta_offset + romfs_header.file_meta_size,
        )
        metadata_aligned_start = metadata_start & ~0xF
        metadata = read_plain(
            level_offset + metadata_aligned_start,
            metadata_end - metadata_aligned_start,
        )
        files = parse_romfs_files(romfs_header, metadata, metadata_aligned_start)
        archive = next((entry for entry in files if entry.path == "/arc/data.trpfs"), None)
        if archive is None:
            raise RuntimeError("The RomFS contains no /arc/data.trpfs archive")
        archive_base = level_offset + romfs_header.data_offset + archive.offset

        def read_archive(offset: int, size: int) -> bytes:
            if offset < 0 or size < 0 or offset + size > archive.size:
                raise RuntimeError("Requested package range extends beyond data.trpfs")
            return read_plain(archive_base + offset, size)

        archive_header = read_archive(0, 16)
        if archive_header[:8] != b"ONEPACK\0":
            raise RuntimeError("The Trinity data archive signature is invalid")
        offsets_start = struct.unpack_from("<Q", archive_header, 8)[0]
        map_header = read_archive(offsets_start + 28, 4)
        pack_count = struct.unpack_from("<I", map_header)[0]
        if not 0 < pack_count < 1_000_000:
            raise RuntimeError("The Trinity package offset count is invalid")
        offset_data = read_archive(offsets_start + 32, pack_count * 8 + 8)
        pack_offsets = list(struct.unpack_from(f"<{pack_count}Q", offset_data))
        hash_count = struct.unpack_from("<I", offset_data, pack_count * 8 + 4)[0]
        if hash_count != pack_count:
            raise RuntimeError("The Trinity package hash and offset counts differ")
        hash_data = read_archive(offsets_start + 40 + pack_count * 8, hash_count * 8)
        pack_hashes = struct.unpack_from(f"<{hash_count}Q", hash_data)
        hash_to_offset = dict(zip(pack_hashes, pack_offsets))
        sorted_offsets = sorted(set(pack_offsets))
        next_offset = {
            value: sorted_offsets[index + 1] if index + 1 < len(sorted_offsets) else archive.size
            for index, value in enumerate(sorted_offsets)
        }

        selected: list[tuple[str, int, int]] = []
        for name in parse_names(args.descriptor.read_bytes()):
            if not any(pattern.search(name) for pattern in patterns):
                continue
            offset = hash_to_offset.get(fnv1a_64(name.encode("utf-8")))
            if offset is None:
                continue
            selected.append((name, offset, next_offset[offset] - offset))

        if not selected:
            raise RuntimeError("No requested Trinity packages were found")
        for name, offset, size in selected:
            destination = args.output / Path(name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(read_archive(offset, size))
            print(f"Extracted {name} ({size:,} bytes)")
        print(f"Extracted {len(selected)} targeted packages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
