#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from extract_botw_exefs import decrypt_xts, main as extract_nca_exefs, read_keys
from extract_nsp_romfs_files import parse_pfs0, pull_range


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", type=Path)
    parser.add_argument("--device")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--nsp")
    source.add_argument("--local-nsp", type=Path)
    parser.add_argument("--prod-keys", type=Path, required=True)
    parser.add_argument("--title-keys", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--title-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.nsp is not None and (args.adb is None or args.device is None):
        parser.error("--adb and --device are required with --nsp")

    def read_range(offset: int, size: int, destination: Path) -> None:
        if args.local_nsp is None:
            pull_range(args.adb, args.device, args.nsp, offset, size, destination)
            return
        with args.local_nsp.open("rb") as source_file:
            source_file.seek(offset)
            data = source_file.read(size)
        if len(data) != size:
            raise RuntimeError(
                f"Local NSP range was short: expected {size}, got {len(data)}"
            )
        destination.write_bytes(data)

    args.cache.mkdir(parents=True, exist_ok=True)
    pfs_header_path = args.cache / "nsp_pfs_header.bin"
    if not pfs_header_path.exists():
        read_range(0, 0x10000, pfs_header_path)
    pfs_data_offset, pfs_files = parse_pfs0(pfs_header_path.read_bytes())
    production = read_keys(args.prod_keys)
    header_key = bytes.fromhex(production["header_key"])
    requested_title = int(args.title_id, 16)

    selected = None
    selected_header = None
    for entry in pfs_files:
        if not entry.name.endswith(".nca") or entry.name.endswith(".cnmt.nca"):
            continue
        header_path = args.cache / f"{entry.name}.header.bin"
        if not header_path.exists():
            read_range(
                pfs_data_offset + entry.offset,
                0xC00,
                header_path,
            )
        header = decrypt_xts(header_path.read_bytes(), header_key)
        if header[0x200:0x204] != b"NCA3":
            continue
        content_type = header[0x205]
        program_id = int.from_bytes(header[0x210:0x218], "little")
        print(
            f"{entry.name}: type={content_type} program_id={program_id:016X} "
            f"size=0x{entry.size:x}"
        )
        if content_type == 0 and program_id == requested_title:
            selected = entry
            selected_header = header
            break

    if selected is None or selected_header is None:
        raise RuntimeError("The requested program NCA was not found")

    title_keys_path = args.title_keys
    requested_rights_id = selected_header[0x230:0x240].hex()
    title_keys = read_keys(args.title_keys)
    if requested_rights_id != "0" * 32 and requested_rights_id not in title_keys:
        ticket_entry = next(
            (entry for entry in pfs_files if entry.name.endswith(".tik")), None
        )
        if ticket_entry is None:
            raise RuntimeError("The NSP has no ticket for its missing title key")
        ticket_path = args.cache / ticket_entry.name
        if not ticket_path.exists():
            read_range(
                pfs_data_offset + ticket_entry.offset,
                ticket_entry.size,
                ticket_path,
            )
        ticket = ticket_path.read_bytes()
        if len(ticket) < 0x2B0:
            raise RuntimeError("The NSP ticket is truncated")
        ticket_rights_id = ticket[0x2A0:0x2B0].hex()
        if ticket_rights_id != requested_rights_id:
            raise RuntimeError("The NSP ticket rights ID does not match the program NCA")
        encrypted_title_key = ticket[0x180:0x190].hex()
        title_keys[requested_rights_id] = encrypted_title_key
        title_keys_path = args.cache / "merged-title.keys"
        title_keys_path.write_text(
            "\n".join(f"{name} = {value}" for name, value in sorted(title_keys.items())) + "\n",
            encoding="utf-8",
        )

    section = None
    for index in range(4):
        start_media = int.from_bytes(
            selected_header[0x240 + index * 0x10 : 0x244 + index * 0x10], "little"
        )
        end_media = int.from_bytes(
            selected_header[0x244 + index * 0x10 : 0x248 + index * 0x10], "little"
        )
        if not start_media:
            continue
        fs_header = selected_header[0x400 + index * 0x200 : 0x600 + index * 0x200]
        if fs_header[2] == 1 and fs_header[3] == 2 and fs_header[4] == 3:
            section = start_media * 0x200, end_media * 0x200
            break
    if section is None:
        raise RuntimeError("The selected program NCA has no ExeFS section")

    section_start, section_end = section
    section_path = args.cache / f"{selected.name}.exefs.enc"
    if not section_path.exists():
        read_range(
            pfs_data_offset + selected.offset + section_start,
            section_end - section_start,
            section_path,
        )

    header_path = args.cache / f"{selected.name}.header.bin"
    import sys

    previous = sys.argv
    try:
        sys.argv = [
            "extract_botw_exefs.py",
            "--nca",
            str(header_path),
            "--exefs-section",
            str(section_path),
            "--prod-keys",
            str(args.prod_keys),
            "--title-keys",
            str(title_keys_path),
            "--output",
            str(args.output),
        ]
        return extract_nca_exefs()
    finally:
        sys.argv = previous


if __name__ == "__main__":
    raise SystemExit(main())
