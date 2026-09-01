#!/usr/bin/env python3








from __future__ import annotations

import argparse
import fnmatch
import shlex
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path

from Crypto.Cipher import AES


RIGHTS_ID = "01007ef00011e0000000000000000000"
NSP_NCA_OFFSET = 0xB90
ROMFS_SECTION_OFFSET = 0x1C000
ROMFS_LEVEL5_OFFSET = 0x1AF8000
ROMFS_HEADER_NSP_OFFSET = NSP_NCA_OFFSET + ROMFS_SECTION_OFFSET + ROMFS_LEVEL5_OFFSET
ROMFS_METADATA_OFFSET = 0x35B594CF0
ROMFS_METADATA_SIZE = 0x26A46C
EMPTY_ENTRY = 0xFFFFFFFF


@dataclass(frozen=True)
class RomFsHeader:
    directory_meta_offset: int
    directory_meta_size: int
    file_meta_offset: int
    file_meta_size: int
    data_offset: int


@dataclass(frozen=True)
class RomFsFile:
    path: str
    offset: int
    size: int


def read_keys(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            name, value = line.split("=", 1)
            result[name.strip().lower()] = value.strip()
    return result


def get_content_key(
    prod_keys: Path, title_keys: Path | None = None, ticket: Path | None = None
) -> bytes:
    production = read_keys(prod_keys)
    if "titlekek_00" not in production:
        raise RuntimeError("Required BOTW/titlekek keys are missing")
    if ticket is not None:
        ticket_data = ticket.read_bytes()
        if len(ticket_data) < 0x2B0 or ticket_data[0x2A0:0x2B0].hex() != RIGHTS_ID:
            raise RuntimeError("The supplied ticket is not BOTW's launch-title ticket")
        encrypted_title_key = ticket_data[0x180:0x190]
    elif title_keys is not None:
        titles = read_keys(title_keys)
        if RIGHTS_ID not in titles:
            raise RuntimeError("The BOTW title key is missing")
        encrypted_title_key = bytes.fromhex(titles[RIGHTS_ID])
    else:
        raise RuntimeError("Either a BOTW ticket or title.keys is required")
    title_kek = bytes.fromhex(production["titlekek_00"])
    return AES.new(title_kek, AES.MODE_ECB).decrypt(encrypted_title_key)


def decrypt_ctr(content_key: bytes, encrypted: bytes, absolute_nca_offset: int) -> bytes:
    if absolute_nca_offset & 0xF:
        raise ValueError("AES-CTR source offset must be 16-byte aligned")
    return AES.new(
        content_key,
        AES.MODE_CTR,
        nonce=b"",
        initial_value=absolute_nca_offset // 16,
    ).decrypt(encrypted)


def adb_shell(adb: Path, device: str, command: str, *, quiet: bool = False) -> None:
    subprocess.run(
        [str(adb), "-s", device, "shell", command],
        check=True,
        stdout=subprocess.DEVNULL if quiet else None,
    )


def pull_range(
    adb: Path,
    device: str,
    nsp: str,
    nsp_offset: int,
    size: int,
    destination: Path,
) -> None:
    remote = f"/sdcard/Download/botw_dualscreen_{nsp_offset:x}.bin"
    quoted_nsp = shlex.quote(nsp)
    quoted_remote = shlex.quote(remote)
    command = (
        f"dd if={quoted_nsp} of={quoted_remote} iflag=skip_bytes,count_bytes "
        f"skip={nsp_offset} count={size}"
    )
    adb_shell(adb, device, command, quiet=True)
    try:
        subprocess.run(
            [str(adb), "-s", device, "pull", remote, str(destination)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    finally:
        adb_shell(adb, device, f"rm -f {quoted_remote}", quiet=True)
    if destination.stat().st_size != size:
        raise RuntimeError(f"ADB range pull was short: expected {size}, got {destination.stat().st_size}")


def parse_header(content_key: bytes, encrypted_header: bytes) -> RomFsHeader:
    absolute_nca_offset = ROMFS_SECTION_OFFSET + ROMFS_LEVEL5_OFFSET
    plain = decrypt_ctr(content_key, encrypted_header, absolute_nca_offset)
    values = struct.unpack_from("<10Q", plain)
    if values[0] != 0x50:
        raise RuntimeError("The decrypted data is not a valid Switch RomFS header")
    return RomFsHeader(
        directory_meta_offset=values[3],
        directory_meta_size=values[4],
        file_meta_offset=values[7],
        file_meta_size=values[8],
        data_offset=values[9],
    )


def parse_files(header: RomFsHeader, metadata: bytes) -> list[RomFsFile]:
    metadata_base = ROMFS_METADATA_OFFSET
    directory_start = header.directory_meta_offset - metadata_base
    file_start = header.file_meta_offset - metadata_base
    if min(directory_start, file_start) < 0:
        raise RuntimeError("RomFS metadata offsets precede the downloaded metadata range")

    directories: dict[int, tuple[int, str]] = {}
    offset = 0
    while offset < header.directory_meta_size:
        entry = directory_start + offset
        parent, _, _, _, _, name_size = struct.unpack_from("<6I", metadata, entry)
        raw_name = metadata[entry + 24 : entry + 24 + name_size]
        directories[offset] = (parent, raw_name.decode("utf-8"))
        offset = (offset + 24 + name_size + 3) & ~3

    def directory_path(directory_offset: int) -> str:
        components: list[str] = []
        visited: set[int] = set()
        while directory_offset != EMPTY_ENTRY:
            if directory_offset in visited or directory_offset not in directories:
                raise RuntimeError("Invalid RomFS directory parent chain")
            visited.add(directory_offset)
            parent, name = directories[directory_offset]
            if name:
                components.append(name)
            if directory_offset == 0:
                break
            directory_offset = parent
        return "/" + "/".join(reversed(components))

    files: list[RomFsFile] = []
    offset = 0
    while offset < header.file_meta_size:
        entry = file_start + offset
        parent, _, data_offset, size, _, name_size = struct.unpack_from(
            "<IIQQII", metadata, entry
        )
        raw_name = metadata[entry + 32 : entry + 32 + name_size]
        name = raw_name.decode("utf-8")
        parent_path = directory_path(parent).rstrip("/")
        files.append(RomFsFile(f"{parent_path}/{name}", data_offset, size))
        offset = (offset + 32 + name_size + 3) & ~3
    return files


def extract_files(
    entries: list[RomFsFile],
    header: RomFsHeader,
    content_key: bytes,
    adb: Path,
    device: str,
    nsp: str,
    output: Path,
    quiet: bool,
) -> None:
    base = ROMFS_SECTION_OFFSET + ROMFS_LEVEL5_OFFSET + header.data_offset
    ranges = sorted((base + entry.offset, base + entry.offset + entry.size, entry) for entry in entries)
    groups: list[tuple[int, int, list[RomFsFile]]] = []
    for start, end, entry in ranges:
        aligned_start = start & ~0xF
        aligned_end = (end + 0xF) & ~0xF
        if groups and aligned_start <= groups[-1][1] + 0x4000:
            group_start, group_end, group_entries = groups[-1]
            groups[-1] = (group_start, max(group_end, aligned_end), group_entries + [entry])
        else:
            groups.append((aligned_start, aligned_end, [entry]))

    with tempfile.TemporaryDirectory(prefix="botw-romfs-") as temporary:
        temporary_path = Path(temporary)
        for group_index, (group_start, group_end, group_entries) in enumerate(groups):
            encrypted_path = temporary_path / f"range-{group_index}.bin"
            size = group_end - group_start
            if not quiet:
                print(f"Pulling {len(group_entries)} files in one {size:,}-byte range")
            pull_range(adb, device, nsp, NSP_NCA_OFFSET + group_start, size, encrypted_path)
            decrypted = decrypt_ctr(content_key, encrypted_path.read_bytes(), group_start)
            for entry in group_entries:
                entry_start = base + entry.offset - group_start
                destination = output / entry.path.lstrip("/")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(decrypted[entry_start : entry_start + entry.size])
                if not quiet:
                    print(f"Extracted {entry.path} ({entry.size:,} bytes)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", type=Path, required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--nsp", required=True, help="Absolute NSP path on the Android device")
    parser.add_argument("--prod-keys", type=Path, required=True)
    title_source = parser.add_mutually_exclusive_group(required=True)
    title_source.add_argument("--title-keys", type=Path)
    title_source.add_argument("--ticket", type=Path)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--match", action="append", default=[], help="Case-insensitive glob")
    parser.add_argument(
        "--path-list",
        type=Path,
        help="UTF-8 file containing exact RomFS paths, one per line",
    )
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.cache.mkdir(parents=True, exist_ok=True)
    encrypted_header_path = args.cache / "botw_romfs_header.enc"
    encrypted_metadata_path = args.cache / "botw_romfs_metadata.enc"
    if not encrypted_header_path.exists():
        pull_range(args.adb, args.device, args.nsp, ROMFS_HEADER_NSP_OFFSET, 0x1000, encrypted_header_path)
    if not encrypted_metadata_path.exists():
        pull_range(
            args.adb,
            args.device,
            args.nsp,
            NSP_NCA_OFFSET + ROMFS_SECTION_OFFSET + ROMFS_LEVEL5_OFFSET + ROMFS_METADATA_OFFSET,
            ROMFS_METADATA_SIZE,
            encrypted_metadata_path,
        )

    content_key = get_content_key(args.prod_keys, args.title_keys, args.ticket)
    header = parse_header(content_key, encrypted_header_path.read_bytes())
    metadata_nca_offset = ROMFS_SECTION_OFFSET + ROMFS_LEVEL5_OFFSET + ROMFS_METADATA_OFFSET
    metadata = decrypt_ctr(content_key, encrypted_metadata_path.read_bytes(), metadata_nca_offset)
    files = parse_files(header, metadata)
    patterns = [pattern.lower() for pattern in args.match]
    exact_paths: set[str] = set()
    if args.path_list is not None:
        exact_paths = {
            line.strip().replace("\\", "/").lower()
            for line in args.path_list.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        }
        exact_paths = {path if path.startswith("/") else f"/{path}" for path in exact_paths}
    if not patterns and not exact_paths:
        patterns = ["*"]
    selected = [
        entry
        for entry in files
        if entry.path.lower() in exact_paths
        or any(fnmatch.fnmatch(entry.path.lower(), pattern) for pattern in patterns)
    ]
    if not args.quiet:
        for entry in selected:
            print(f"{entry.path} | offset=0x{entry.offset:x} size=0x{entry.size:x}")
    print(
        f"Matched {len(selected)} of {len(files)} RomFS files "
        f"({sum(entry.size for entry in selected):,} bytes)"
    )

    if args.extract:
        if args.output is None:
            parser.error("--extract requires --output")
        extract_files(
            selected,
            header,
            content_key,
            args.adb,
            args.device,
            args.nsp,
            args.output,
            args.quiet,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
