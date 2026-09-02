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

from extract_botw_exefs import content_key, decrypt_ctr, decrypt_xts, read_keys


MEDIA_UNIT = 0x200
EMPTY_ENTRY = 0xFFFFFFFF


@dataclass(frozen=True)
class PfsFile:
    name: str
    offset: int
    size: int


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
    remote = f"/sdcard/Download/eden_romfs_{nsp_offset:x}.bin"
    command = (
        f"dd if={shlex.quote(nsp)} of={shlex.quote(remote)} iflag=skip_bytes,count_bytes "
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
        adb_shell(adb, device, f"rm -f {shlex.quote(remote)}", quiet=True)
    if destination.stat().st_size != size:
        raise RuntimeError(
            f"ADB range pull was short: expected {size}, got {destination.stat().st_size}"
        )


def read_local_range(source: Path, offset: int, size: int, destination: Path) -> None:
    with source.open("rb") as input_file, destination.open("wb") as output_file:
        input_file.seek(offset)
        remaining = size
        while remaining:
            block = input_file.read(min(remaining, 8 * 1024 * 1024))
            if not block:
                break
            output_file.write(block)
            remaining -= len(block)
    if destination.stat().st_size != size:
        raise RuntimeError(
            f"Local NSP range was short: expected {size}, got {destination.stat().st_size}"
        )


def parse_pfs0(data: bytes) -> tuple[int, list[PfsFile]]:
    if data[:4] != b"PFS0":
        raise RuntimeError("The NSP does not begin with a PFS0 header")
    file_count, string_size = struct.unpack_from("<II", data, 4)
    entries_offset = 0x10
    strings_offset = entries_offset + file_count * 0x18
    metadata_size = strings_offset + string_size
    if metadata_size > len(data):
        raise RuntimeError("The downloaded NSP header is truncated")
    # PFS0 file data begins immediately after the string table. Individual
    # packages may choose aligned string-table sizes, but the format does not
    # add implicit padding here.
    data_offset = metadata_size
    files: list[PfsFile] = []
    for index in range(file_count):
        offset, size, name_offset = struct.unpack_from(
            "<QQI", data, entries_offset + index * 0x18
        )
        name_start = strings_offset + name_offset
        name_end = data.find(b"\0", name_start, metadata_size)
        if name_end < 0:
            raise RuntimeError("An NSP filename is unterminated")
        files.append(PfsFile(data[name_start:name_end].decode("utf-8"), offset, size))
    return data_offset, files


def find_romfs_level(header: bytes) -> tuple[int, int, bytes]:
    for index in range(4):
        start_media, end_media = struct.unpack_from("<II", header, 0x240 + index * 0x10)
        if not start_media:
            continue
        fs_header = header[0x400 + index * 0x200 : 0x600 + index * 0x200]
        if fs_header[3] != 3 or fs_header[4] != 3 or fs_header[8:12] != b"IVFC":
            continue
        level_count = struct.unpack_from("<I", fs_header, 0x14)[0]
        levels: list[tuple[int, int]] = []
        for level in range(max(0, level_count - 1)):
            offset, size, _, _ = struct.unpack_from("<QQII", fs_header, 0x18 + level * 0x18)
            if size:
                levels.append((offset, size))
        if not levels:
            continue
        level_offset, level_size = max(levels, key=lambda value: value[1])
        section_start = start_media * MEDIA_UNIT
        section_end = end_media * MEDIA_UNIT
        if level_offset + level_size > section_end - section_start:
            raise RuntimeError("The NCA IVFC level extends beyond its section")
        return section_start + level_offset, level_size, fs_header[0x140:0x148]
    raise RuntimeError("The selected NCA has no supported CTR-encrypted RomFS section")


def parse_romfs_header(data: bytes) -> RomFsHeader:
    values = struct.unpack_from("<10Q", data)
    if values[0] != 0x50:
        raise RuntimeError("The decrypted data is not a valid Switch RomFS header")
    return RomFsHeader(values[3], values[4], values[7], values[8], values[9])


def parse_romfs_files(
    header: RomFsHeader, metadata: bytes, metadata_base: int
) -> list[RomFsFile]:
    directory_start = header.directory_meta_offset - metadata_base
    file_start = header.file_meta_offset - metadata_base
    if min(directory_start, file_start) < 0:
        raise RuntimeError("RomFS metadata offsets precede the downloaded range")

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
        name = metadata[entry + 32 : entry + 32 + name_size].decode("utf-8")
        files.append(
            RomFsFile(f"{directory_path(parent).rstrip('/')}/{name}", data_offset, size)
        )
        offset = (offset + 32 + name_size + 3) & ~3
    return files


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--device")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--nsp", help="NSP path on an ADB-connected device")
    source.add_argument("--local-nsp", type=Path, help="NSP already copied to this computer")
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--title-keys", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--nca-name", help="Content NCA filename; defaults to the largest non-CNMT NCA")
    parser.add_argument("--match", action="append", default=[], help="Case-insensitive glob")
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--manifest", type=Path, help="Write matched paths, offsets, and sizes")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    if args.nsp and (args.adb is None or not args.device):
        parser.error("--nsp requires --adb and --device")

    def fetch_range(offset: int, size: int, destination: Path) -> None:
        if args.local_nsp is not None:
            read_local_range(args.local_nsp, offset, size, destination)
        else:
            pull_range(args.adb, args.device, args.nsp, offset, size, destination)

    args.cache.mkdir(parents=True, exist_ok=True)
    pfs_header_path = args.cache / "nsp_pfs_header.bin"
    if not pfs_header_path.exists():
        fetch_range(0, 0x10000, pfs_header_path)
    pfs_data_offset, pfs_files = parse_pfs0(pfs_header_path.read_bytes())
    if args.nca_name:
        candidates = [entry for entry in pfs_files if entry.name == args.nca_name]
    else:
        candidates = [
            entry for entry in pfs_files
            if entry.name.endswith(".nca") and not entry.name.endswith(".cnmt.nca")
        ]
    if not candidates:
        raise RuntimeError("No matching content NCA was found in the NSP")
    nca = max(candidates, key=lambda entry: entry.size)
    nca_nsp_offset = pfs_data_offset + nca.offset

    nca_header_path = args.cache / f"{nca.name}.header.bin"
    if not nca_header_path.exists():
        fetch_range(nca_nsp_offset, 0xC00, nca_header_path)
    encrypted_nca_header = nca_header_path.read_bytes()
    production = read_keys(args.prod_keys)
    nca_header = decrypt_xts(encrypted_nca_header, bytes.fromhex(production["header_key"]))
    if nca_header[0x200:0x204] != b"NCA3":
        raise RuntimeError("NCA header decryption failed")
    titles = read_keys(args.title_keys)
    rights_id = nca_header[0x230:0x240].hex()
    if rights_id != "0" * 32 and rights_id not in titles:
        ticket_entry = next(
            (entry for entry in pfs_files if entry.name.endswith(".tik")), None
        )
        if ticket_entry is None:
            raise RuntimeError("The NSP has no ticket for its missing title key")
        ticket_path = args.cache / ticket_entry.name
        if not ticket_path.exists():
            fetch_range(
                pfs_data_offset + ticket_entry.offset,
                ticket_entry.size,
                ticket_path,
            )
        ticket = ticket_path.read_bytes()
        if len(ticket) < 0x2B0:
            raise RuntimeError("The NSP ticket is truncated")
        if ticket[0x2A0:0x2B0].hex() != rights_id:
            raise RuntimeError("The NSP ticket rights ID does not match the program NCA")
        titles[rights_id] = ticket[0x180:0x190].hex()
    key = content_key(nca_header, production, titles)
    level_offset, level_size, section_ctr = find_romfs_level(nca_header)

    romfs_header_path = args.cache / f"{nca.name}.romfs_header.enc"
    if not romfs_header_path.exists():
        fetch_range(nca_nsp_offset + level_offset, 0x1000, romfs_header_path)
    romfs_header_plain = decrypt_ctr(
        romfs_header_path.read_bytes(), key, section_ctr, level_offset
    )
    romfs_header = parse_romfs_header(romfs_header_plain)
    metadata_start = min(
        romfs_header.directory_meta_offset, romfs_header.file_meta_offset
    )
    metadata_end = max(
        romfs_header.directory_meta_offset + romfs_header.directory_meta_size,
        romfs_header.file_meta_offset + romfs_header.file_meta_size,
    )
    aligned_metadata_start = metadata_start & ~0xF


    aligned_metadata_end = metadata_end
    if aligned_metadata_end > level_size:
        raise RuntimeError("RomFS metadata extends beyond its IVFC level")

    metadata_path = args.cache / f"{nca.name}.romfs_metadata.enc"
    metadata_size = aligned_metadata_end - aligned_metadata_start
    if not metadata_path.exists():
        fetch_range(
            nca_nsp_offset + level_offset + aligned_metadata_start,
            metadata_size, metadata_path,
        )
    metadata = decrypt_ctr(
        metadata_path.read_bytes(), key, section_ctr, level_offset + aligned_metadata_start
    )
    files = parse_romfs_files(romfs_header, metadata, aligned_metadata_start)
    patterns = [pattern.lower() for pattern in args.match] or ["*"]
    selected = [
        entry for entry in files
        if any(fnmatch.fnmatch(entry.path.lower(), pattern) for pattern in patterns)
    ]
    if args.manifest is not None:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(
            "".join(
                f"{entry.path}\toffset=0x{entry.offset:x}\tsize=0x{entry.size:x}\n"
                for entry in selected
            ),
            encoding="utf-8",
        )
    if not args.quiet:
        for entry in selected:
            print(f"{entry.path} | offset=0x{entry.offset:x} size=0x{entry.size:x}")
    print(f"Matched {len(selected)} of {len(files)} RomFS files")

    if not args.extract:
        return 0
    if args.output is None:
        parser.error("--extract requires --output")

    data_base = level_offset + romfs_header.data_offset
    ranges = sorted(
        (data_base + entry.offset, data_base + entry.offset + entry.size, entry)
        for entry in selected
    )
    groups: list[tuple[int, int, list[RomFsFile]]] = []
    for start, end, entry in ranges:
        aligned_start = start & ~0xF
        aligned_end = (end + 0xF) & ~0xF
        if groups and aligned_start <= groups[-1][1] + 0x4000:
            group_start, group_end, group_entries = groups[-1]
            groups[-1] = (group_start, max(group_end, aligned_end), group_entries + [entry])
        else:
            groups.append((aligned_start, aligned_end, [entry]))

    with tempfile.TemporaryDirectory(prefix="eden-romfs-") as temporary:
        temporary_path = Path(temporary)
        for group_index, (group_start, group_end, group_entries) in enumerate(groups):
            encrypted_path = temporary_path / f"range-{group_index}.bin"
            size = group_end - group_start
            if not args.quiet:
                print(f"Pulling {len(group_entries)} files in one {size:,}-byte range")
            fetch_range(nca_nsp_offset + group_start, size, encrypted_path)
            decrypted = decrypt_ctr(
                encrypted_path.read_bytes(), key, section_ctr, group_start
            )
            for entry in group_entries:
                entry_start = data_base + entry.offset - group_start
                destination = args.output / entry.path.lstrip("/")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(decrypted[entry_start : entry_start + entry.size])
                if not args.quiet:
                    print(f"Extracted {entry.path} ({entry.size:,} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
