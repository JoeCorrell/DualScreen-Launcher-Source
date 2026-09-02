#!/usr/bin/env python3

"""Extract selected Xenoblade Chronicles X: DE archive members from an NSP.

The game's RomFS stores most resources in sts.ard and indexes them with the
small sts.arh file.  Pulling the complete ARD is wasteful (and, over ADB,
painfully slow), so this tool resolves requested path hashes locally and reads
only the corresponding encrypted NCA byte ranges.

This is an interoperability/research utility.  It requires keys and game files
supplied by the user and deliberately has no bulk-extraction mode.
"""

from __future__ import annotations

import argparse
import shlex
import struct
import subprocess
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

from extract_botw_exefs import content_key, decrypt_ctr, decrypt_xts, read_keys
from extract_nsp_romfs_files import (
    MEDIA_UNIT,
    find_romfs_level,
    parse_pfs0,
    parse_romfs_files,
    parse_romfs_header,
    pull_range,
    read_local_range,
)


MASK64 = (1 << 64) - 1
XXH64_PRIME1 = 11400714785074694791
XXH64_PRIME2 = 14029467366897019727
XXH64_PRIME3 = 1609587929392839161
XXH64_PRIME4 = 9650029242287828579
XXH64_PRIME5 = 2870177450012600261


@dataclass(frozen=True)
class ArchiveFile:
    path_hash: int
    disk_size: int
    expanded_size: int
    offset: int


def rotate_left(value: int, count: int) -> int:
    return ((value << count) | (value >> (64 - count))) & MASK64


def xxh64_round(accumulator: int, lane: int) -> int:
    accumulator = (accumulator + lane * XXH64_PRIME2) & MASK64
    accumulator = rotate_left(accumulator, 31)
    return (accumulator * XXH64_PRIME1) & MASK64


def xxh64_merge_round(accumulator: int, lane: int) -> int:
    accumulator ^= xxh64_round(0, lane)
    return (accumulator * XXH64_PRIME1 + XXH64_PRIME4) & MASK64


def xxh64(data: bytes, seed: int = 0) -> int:
    """Return the standard XXH64 digest as an integer."""
    length = len(data)
    cursor = 0
    if length >= 32:
        v1 = (seed + XXH64_PRIME1 + XXH64_PRIME2) & MASK64
        v2 = (seed + XXH64_PRIME2) & MASK64
        v3 = seed & MASK64
        v4 = (seed - XXH64_PRIME1) & MASK64
        limit = length - 32
        while cursor <= limit:
            v1 = xxh64_round(v1, struct.unpack_from("<Q", data, cursor)[0])
            v2 = xxh64_round(v2, struct.unpack_from("<Q", data, cursor + 8)[0])
            v3 = xxh64_round(v3, struct.unpack_from("<Q", data, cursor + 16)[0])
            v4 = xxh64_round(v4, struct.unpack_from("<Q", data, cursor + 24)[0])
            cursor += 32
        value = (
            rotate_left(v1, 1)
            + rotate_left(v2, 7)
            + rotate_left(v3, 12)
            + rotate_left(v4, 18)
        ) & MASK64
        value = xxh64_merge_round(value, v1)
        value = xxh64_merge_round(value, v2)
        value = xxh64_merge_round(value, v3)
        value = xxh64_merge_round(value, v4)
    else:
        value = (seed + XXH64_PRIME5) & MASK64

    value = (value + length) & MASK64
    while cursor + 8 <= length:
        lane = xxh64_round(0, struct.unpack_from("<Q", data, cursor)[0])
        value ^= lane
        value = (rotate_left(value, 27) * XXH64_PRIME1 + XXH64_PRIME4) & MASK64
        cursor += 8
    if cursor + 4 <= length:
        value ^= (
            struct.unpack_from("<I", data, cursor)[0] * XXH64_PRIME1
        ) & MASK64
        value &= MASK64
        value = (rotate_left(value, 23) * XXH64_PRIME2 + XXH64_PRIME3) & MASK64
        cursor += 4
    while cursor < length:
        value ^= (data[cursor] * XXH64_PRIME5) & MASK64
        value &= MASK64
        value = (rotate_left(value, 11) * XXH64_PRIME1) & MASK64
        cursor += 1

    value ^= value >> 33
    value = (value * XXH64_PRIME2) & MASK64
    value ^= value >> 29
    value = (value * XXH64_PRIME3) & MASK64
    value ^= value >> 32
    return value


def hash_path(path: str) -> int:
    normalized = path.replace("\\", "/").lower()
    if not normalized.startswith("/"):
        normalized = "/" + normalized
    return xxh64(normalized.encode("ascii"))


def parse_arh2(path: Path) -> tuple[int, dict[int, ArchiveFile]]:
    data = path.read_bytes()
    if data[:4] != b"arh2" or len(data) < 16:
        raise RuntimeError(f"{path} is not an ARH2 archive index")
    file_count, alignment = struct.unpack_from("<II", data, 4)
    expected_size = 16 + file_count * 16
    if len(data) < expected_size:
        raise RuntimeError(
            f"The ARH2 index is truncated: expected at least {expected_size} bytes"
        )
    if alignment == 0 or alignment & (alignment - 1):
        raise RuntimeError(f"Invalid ARH2 alignment: {alignment}")

    files: dict[int, ArchiveFile] = {}
    archive_offset = 0
    for index in range(file_count):
        path_hash, disk_size, expanded_size = struct.unpack_from(
            "<QII", data, 16 + index * 16
        )
        files[path_hash] = ArchiveFile(
            path_hash, disk_size, expanded_size, archive_offset
        )
        archive_offset = (archive_offset + disk_size + alignment - 1) & -alignment
    return alignment, files


def decompress_xbc1(data: bytes, expected_size: int | None) -> bytes:
    if len(data) < 0x30 or data[:4] != b"xbc1":
        raise RuntimeError("A compressed ARD member is not an Xbc1 stream")
    compression_type, expanded_size, compressed_size = struct.unpack_from("<III", data, 4)
    if expected_size and expanded_size != expected_size:
        raise RuntimeError(
            f"Xbc1 expanded size mismatch: index={expected_size}, header={expanded_size}"
        )
    payload = data[0x30 : 0x30 + compressed_size]
    if len(payload) != compressed_size:
        raise RuntimeError("The Xbc1 payload is truncated")
    if compression_type == 1:
        result = zlib.decompress(payload)
    elif compression_type == 3:
        try:
            import zstandard  # type: ignore[import-not-found]
        except ImportError as error:
            raise RuntimeError(
                "This member uses Zstandard; install the Python 'zstandard' package"
            ) from error
        result = zstandard.ZstdDecompressor().decompress(
            payload, max_output_size=expanded_size
        )
    else:
        raise RuntimeError(f"Unsupported Xbc1 compression type {compression_type}")
    if len(result) != expanded_size:
        raise RuntimeError(
            f"Xbc1 decompression was short: expected {expanded_size}, got {len(result)}"
        )
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--device")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--nsp", help="NSP path on an ADB-connected device")
    source.add_argument("--local-nsp", type=Path)
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--title-keys", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--arh", type=Path, required=True)
    parser.add_argument("--path", action="append", default=[])
    parser.add_argument("--hash", action="append", default=[])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--nca-name")
    parser.add_argument("--keep-xbc", action="store_true")
    args = parser.parse_args()

    if args.nsp and (args.adb is None or not args.device):
        parser.error("--nsp requires --adb and --device")
    if not args.path and not args.hash:
        parser.error("At least one --path or --hash is required")

    def fetch_range(offset: int, size: int, destination: Path) -> None:
        if args.local_nsp is not None:
            read_local_range(args.local_nsp, offset, size, destination)
        else:
            pull_range(args.adb, args.device, args.nsp, offset, size, destination)

    alignment, archive_files = parse_arh2(args.arh)
    requests: list[tuple[str, ArchiveFile]] = []
    seen: set[int] = set()
    for requested_path in args.path:
        path_hash = hash_path(requested_path)
        entry = archive_files.get(path_hash)
        if entry is None:
            raise RuntimeError(
                f"Archive member not found: {requested_path} ({path_hash:016X})"
            )
        if path_hash not in seen:
            requests.append((requested_path, entry))
            seen.add(path_hash)
    for requested_hash in args.hash:
        path_hash = int(requested_hash, 16)
        entry = archive_files.get(path_hash)
        if entry is None:
            raise RuntimeError(f"Archive hash not found: {path_hash:016X}")
        if path_hash not in seen:
            requests.append((f".unmapped/{path_hash:016X}.bin", entry))
            seen.add(path_hash)

    args.cache.mkdir(parents=True, exist_ok=True)
    pfs_header_path = args.cache / "nsp_pfs_header.bin"
    if not pfs_header_path.exists():
        fetch_range(0, 0x10000, pfs_header_path)
    pfs_data_offset, pfs_files = parse_pfs0(pfs_header_path.read_bytes())
    if args.nca_name:
        candidates = [entry for entry in pfs_files if entry.name == args.nca_name]
    else:
        candidates = [
            entry
            for entry in pfs_files
            if entry.name.endswith(".nca") and not entry.name.endswith(".cnmt.nca")
        ]
    if not candidates:
        raise RuntimeError("No matching content NCA was found")
    nca = max(candidates, key=lambda entry: entry.size)
    nca_nsp_offset = pfs_data_offset + nca.offset

    nca_header_path = args.cache / f"{nca.name}.header.bin"
    if not nca_header_path.exists():
        fetch_range(nca_nsp_offset, 0xC00, nca_header_path)
    production = read_keys(args.prod_keys)
    nca_header = decrypt_xts(
        nca_header_path.read_bytes(), bytes.fromhex(production["header_key"])
    )
    if nca_header[0x200:0x204] != b"NCA3":
        raise RuntimeError("NCA header decryption failed")
    titles = read_keys(args.title_keys)
    rights_id = nca_header[0x230:0x240].hex()
    if rights_id != "0" * 32 and rights_id not in titles:
        raise RuntimeError(f"Missing title key for rights ID {rights_id}")
    key = content_key(nca_header, production, titles)
    level_offset, level_size, section_ctr = find_romfs_level(nca_header)

    romfs_header_path = args.cache / f"{nca.name}.romfs_header.enc"
    if not romfs_header_path.exists():
        fetch_range(nca_nsp_offset + level_offset, 0x1000, romfs_header_path)
    romfs_header = parse_romfs_header(
        decrypt_ctr(romfs_header_path.read_bytes(), key, section_ctr, level_offset)
    )
    metadata_start = min(
        romfs_header.directory_meta_offset, romfs_header.file_meta_offset
    ) & ~0xF
    metadata_end = max(
        romfs_header.directory_meta_offset + romfs_header.directory_meta_size,
        romfs_header.file_meta_offset + romfs_header.file_meta_size,
    )
    if metadata_end > level_size:
        raise RuntimeError("RomFS metadata extends beyond its IVFC level")
    metadata_path = args.cache / f"{nca.name}.romfs_metadata.enc"
    if not metadata_path.exists():
        fetch_range(
            nca_nsp_offset + level_offset + metadata_start,
            metadata_end - metadata_start,
            metadata_path,
        )
    metadata = decrypt_ctr(
        metadata_path.read_bytes(), key, section_ctr, level_offset + metadata_start
    )
    romfs_files = parse_romfs_files(romfs_header, metadata, metadata_start)
    ard = next((entry for entry in romfs_files if entry.path.lower() == "/sts.ard"), None)
    if ard is None:
        raise RuntimeError("The program RomFS does not contain /sts.ard")
    archive_span = max(
        entry.offset + ((entry.disk_size + alignment - 1) & -alignment)
        for entry in archive_files.values()
    )
    if archive_span > ard.size:
        raise RuntimeError(
            f"ARH2 describes 0x{archive_span:x} bytes, beyond sts.ard size 0x{ard.size:x}"
        )

    romfs_data_base = level_offset + romfs_header.data_offset
    ard_base = romfs_data_base + ard.offset
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xcx-ard-") as temporary:
        temporary_path = Path(temporary)
        for index, (requested_path, entry) in enumerate(requests):
            member_start = ard_base + entry.offset
            aligned_start = member_start & ~0xF
            aligned_end = (member_start + entry.disk_size + 0xF) & ~0xF
            encrypted_path = temporary_path / f"member-{index}.enc"
            fetch_range(
                nca_nsp_offset + aligned_start,
                aligned_end - aligned_start,
                encrypted_path,
            )
            decrypted = decrypt_ctr(
                encrypted_path.read_bytes(), key, section_ctr, aligned_start
            )
            relative_start = member_start - aligned_start
            member = decrypted[relative_start : relative_start + entry.disk_size]
            if len(member) != entry.disk_size:
                raise RuntimeError(f"Archive member was short: {requested_path}")

            is_xbc = member[:4] == b"xbc1"
            if (entry.expanded_size or is_xbc) and not args.keep_xbc:
                member = decompress_xbc1(member, entry.expanded_size or None)
            destination = args.output / requested_path.lstrip("/")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(member)
            print(
                f"Extracted {requested_path} hash={entry.path_hash:016X} "
                f"ard_offset=0x{entry.offset:x} disk=0x{entry.disk_size:x} "
                f"output=0x{len(member):x}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
