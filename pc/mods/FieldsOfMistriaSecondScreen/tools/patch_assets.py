#!/usr/bin/env python3
"""Build an exact-v1.0.4 Fields of Mistria GML bridge archive.

The input archive is never modified. Untouched local ZIP records, compressed data,
and central-directory records are copied verbatim. The old Game.gml local record is
left as unreachable data while its central record is replaced, avoiding a rewrite
of the 600+ MiB asset payload.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import os
import struct
import sys
import uuid
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO


EXPECTED_ARCHIVE_SHA256 = "e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450"
EXPECTED_ARCHIVE_SIZE = 625_619_136
EXPECTED_ENTRY_COUNT = 122_403
GAME_ENTRY = "assets/gml/objects/Game.gml"
BRIDGE_ENTRY = "assets/gml/scripts/DualScreenBridge.gml"
EXPECTED_GAME_SHA256 = "82cccb93e460246b52033b9dc56a76da5460dc27133150bcbfb40a7bdcbb43e8"
PATCH_TOOL_SCHEMA = 1

CENTRAL_SIGNATURE = 0x02014B50
LOCAL_SIGNATURE = 0x04034B50
ZIP64_EOCD_SIGNATURE = 0x06064B50
ZIP64_LOCATOR_SIGNATURE = 0x07064B50
EOCD_SIGNATURE = 0x06054B50
COPY_CHUNK = 4 * 1024 * 1024


class PatchError(RuntimeError):
    pass


@dataclass(frozen=True)
class AnchorPatch:
    name: str
    anchor: bytes
    replacement: bytes


ANCHOR_PATCHES = (
    AnchorPatch(
        "construct_after_load_or_new",
        b"            self.load_state = undefined;\n\n            //\n            //",
        b"            self.load_state = undefined;\n"
        b"            self.dualscreen_bridge = new DualScreenBridge(self.unique_identifier);\n\n"
        b"            //\n            //",
    ),
    AnchorPatch(
        "update_at_step_end",
        b"        step_end: function() {\n            CAMERA.update();",
        b"        step_end: function() {\n"
        b"            if self.dualscreen_bridge != undefined {\n"
        b"                self.dualscreen_bridge.update();\n"
        b"            }\n\n"
        b"            CAMERA.update();",
    ),
    AnchorPatch(
        "notify_after_room_start",
        b"            trace(\n"
        b"                \"Loaded '{}' in {micro}\",\n"
        b"                display_location(CURRENT_LOCATION_ID, CURRENT_DYN_INDEX, room()),\n"
        b"                get_timer() - start_time,\n"
        b"            );\n"
        b"        },\n"
        b"        room_end: function() {",
        b"            trace(\n"
        b"                \"Loaded '{}' in {micro}\",\n"
        b"                display_location(CURRENT_LOCATION_ID, CURRENT_DYN_INDEX, room()),\n"
        b"                get_timer() - start_time,\n"
        b"            );\n\n"
        b"            self.dualscreen_bridge.on_room_start();\n"
        b"        },\n"
        b"        room_end: function() {",
    ),
    AnchorPatch(
        "shutdown_before_world_cleanup",
        b"        cleanup: function() {\n            WEATHER = undefined;",
        b"        cleanup: function() {\n"
        b"            if self.dualscreen_bridge != undefined {\n"
        b"                self.dualscreen_bridge.shutdown();\n"
        b"            }\n\n"
        b"            WEATHER = undefined;",
    ),
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(COPY_CHUNK), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def patch_game_source(original: bytes) -> bytes:
    if sha256_bytes(original) != EXPECTED_GAME_SHA256:
        raise PatchError("Game.gml entry hash does not match the inspected v1.0.4 source")

    patched = original
    for patch in ANCHOR_PATCHES:
        count = patched.count(patch.anchor)
        if count != 1:
            raise PatchError(
                f"Game.gml anchor {patch.name!r} occurred {count} times; expected exactly once"
            )
        patched = patched.replace(patch.anchor, patch.replacement, 1)

    expected_markers = {
        b"new DualScreenBridge(self.unique_identifier)": 1,
        b"self.dualscreen_bridge.update()": 1,
        b"self.dualscreen_bridge.on_room_start()": 1,
        b"self.dualscreen_bridge.shutdown()": 1,
    }
    for marker, expected in expected_markers.items():
        actual = patched.count(marker)
        if actual != expected:
            raise PatchError(f"post-patch marker {marker!r} occurred {actual} times")
    return patched


def validate_input(
    input_path: Path,
    bridge_path: Path,
    *,
    expected_archive_hash: str = EXPECTED_ARCHIVE_SHA256,
    expected_archive_size: int = EXPECTED_ARCHIVE_SIZE,
    expected_entry_count: int = EXPECTED_ENTRY_COUNT,
) -> tuple[bytes, bytes, zipfile.ZipInfo, bytes, int, bytes]:
    if not input_path.is_file():
        raise PatchError(f"input archive does not exist: {input_path}")
    if not bridge_path.is_file():
        raise PatchError(f"bridge source does not exist: {bridge_path}")

    size = input_path.stat().st_size
    if size != expected_archive_size:
        raise PatchError(f"archive size mismatch: got {size}, expected {expected_archive_size}")
    archive_hash = sha256_file(input_path)
    if archive_hash != expected_archive_hash:
        raise PatchError(
            f"archive hash mismatch: got {archive_hash}, expected {expected_archive_hash}"
        )

    with zipfile.ZipFile(input_path, "r") as archive:
        infos = archive.infolist()
        if len(infos) != expected_entry_count:
            raise PatchError(
                f"entry-count mismatch: got {len(infos)}, expected {expected_entry_count}"
            )
        game_infos = [item for item in infos if item.filename == GAME_ENTRY]
        if len(game_infos) != 1:
            raise PatchError(f"expected exactly one {GAME_ENTRY!r} entry")
        if any(item.filename == BRIDGE_ENTRY for item in infos):
            raise PatchError(f"input already contains {BRIDGE_ENTRY!r}; refusing to stack patches")
        original_game = archive.read(game_infos[0])
        patched_game = patch_game_source(original_game)
        bridge = bridge_path.read_bytes()
        if not bridge or b"function DualScreenBridge(" not in bridge:
            raise PatchError("bridge source is empty or missing its constructor")
        return (
            patched_game,
            bridge,
            game_infos[0],
            archive.comment,
            archive.start_dir,
            archive_hash.encode("ascii"),
        )


@dataclass(frozen=True)
class NewEntry:
    name: bytes
    data: bytes
    compressed: bytes
    crc32: int
    offset: int


def make_new_entry(name: str, data: bytes, offset: int) -> NewEntry:
    name_bytes = name.encode("utf-8")
    return NewEntry(
        name=name_bytes,
        data=data,
        compressed=data,
        crc32=binascii.crc32(data) & 0xFFFFFFFF,
        offset=offset,
    )


def local_record(entry: NewEntry) -> bytes:
    header = struct.pack(
        "<IHHHHHIIIHH",
        LOCAL_SIGNATURE,
        20,
        0x0800,
        zipfile.ZIP_STORED,
        0,
        0x0021,
        entry.crc32,
        len(entry.compressed),
        len(entry.data),
        len(entry.name),
        0,
    )
    return header + entry.name + entry.compressed


def central_record(entry: NewEntry) -> bytes:
    return struct.pack(
        "<IHHHHHHIIIHHHHHII",
        CENTRAL_SIGNATURE,
        (3 << 8) | 20,
        20,
        0x0800,
        zipfile.ZIP_STORED,
        0,
        0x0021,
        entry.crc32,
        len(entry.compressed),
        len(entry.data),
        len(entry.name),
        0,
        0,
        0,
        0,
        0o100644 << 16,
        entry.offset,
    ) + entry.name


def copy_exact(source: BinaryIO, target: BinaryIO, length: int) -> str:
    digest = hashlib.sha256()
    remaining = length
    while remaining:
        chunk = source.read(min(COPY_CHUNK, remaining))
        if not chunk:
            raise PatchError("unexpected end of input while copying original local records")
        target.write(chunk)
        digest.update(chunk)
        remaining -= len(chunk)
    return digest.hexdigest()


def read_central_record(source: BinaryIO) -> tuple[bytes, bytes]:
    fixed = source.read(46)
    if len(fixed) != 46:
        raise PatchError("truncated original central directory")
    values = struct.unpack("<IHHHHHHIIIHHHHHII", fixed)
    if values[0] != CENTRAL_SIGNATURE:
        raise PatchError(f"unexpected central signature 0x{values[0]:08x}")
    name_length, extra_length, comment_length = values[10], values[11], values[12]
    variable = source.read(name_length + extra_length + comment_length)
    if len(variable) != name_length + extra_length + comment_length:
        raise PatchError("truncated original central-directory variable fields")
    return fixed + variable, variable[:name_length]


def write_zip64_end(
    target: BinaryIO,
    *,
    entry_count: int,
    central_size: int,
    central_offset: int,
    comment: bytes,
) -> None:
    zip64_offset = target.tell()
    target.write(
        struct.pack(
            "<IQHHIIQQQQ",
            ZIP64_EOCD_SIGNATURE,
            44,
            45,
            45,
            0,
            0,
            entry_count,
            entry_count,
            central_size,
            central_offset,
        )
    )
    target.write(struct.pack("<IIQI", ZIP64_LOCATOR_SIGNATURE, 0, zip64_offset, 1))
    target.write(
        struct.pack(
            "<IHHHHIIH",
            EOCD_SIGNATURE,
            0,
            0,
            min(entry_count, 0xFFFF),
            min(entry_count, 0xFFFF),
            min(central_size, 0xFFFFFFFF),
            min(central_offset, 0xFFFFFFFF),
            len(comment),
        )
    )
    target.write(comment)


def build_archive(
    input_path: Path,
    output_temp: Path,
    *,
    patched_game: bytes,
    bridge: bytes,
    original_count: int,
    original_start_dir: int,
    archive_comment: bytes,
) -> dict[str, object]:
    original_game_name = GAME_ENTRY.encode("utf-8")
    bridge_name = BRIDGE_ENTRY.encode("utf-8")

    with input_path.open("rb") as source, output_temp.open("xb") as target:
        original_prefix_hash = copy_exact(source, target, original_start_dir)

        game_entry = make_new_entry(GAME_ENTRY, patched_game, target.tell())
        target.write(local_record(game_entry))
        bridge_entry = make_new_entry(BRIDGE_ENTRY, bridge, target.tell())
        target.write(local_record(bridge_entry))

        central_offset = target.tell()
        source.seek(original_start_dir)
        copied_central_count = 0
        removed_game_count = 0
        for _ in range(original_count):
            raw_record, raw_name = read_central_record(source)
            if raw_name == original_game_name:
                removed_game_count += 1
                continue
            if raw_name == bridge_name:
                raise PatchError("bridge entry appeared while copying central directory")
            target.write(raw_record)
            copied_central_count += 1

        if removed_game_count != 1:
            raise PatchError(
                f"removed {removed_game_count} Game.gml central records; expected exactly one"
            )
        target.write(central_record(game_entry))
        target.write(central_record(bridge_entry))
        central_size = target.tell() - central_offset
        output_count = copied_central_count + 2
        write_zip64_end(
            target,
            entry_count=output_count,
            central_size=central_size,
            central_offset=central_offset,
            comment=archive_comment,
        )
        target.flush()
        os.fsync(target.fileno())

    return {
        "original_local_region_bytes": original_start_dir,
        "original_local_region_sha256": original_prefix_hash,
        "untouched_central_records_copied": copied_central_count,
        "output_entry_count": output_count,
        "central_offset": central_offset,
        "central_size": central_size,
    }


def verify_output(
    input_path: Path,
    output_path: Path,
    *,
    patched_game: bytes,
    bridge: bytes,
    original_start_dir: int,
    original_prefix_hash: str,
    full_crc: bool,
) -> dict[str, object]:
    prefix_digest = hashlib.sha256()
    with output_path.open("rb") as stream:
        remaining = original_start_dir
        while remaining:
            chunk = stream.read(min(COPY_CHUNK, remaining))
            if not chunk:
                raise PatchError("output ended inside preserved original local region")
            prefix_digest.update(chunk)
            remaining -= len(chunk)
    if prefix_digest.hexdigest() != original_prefix_hash:
        raise PatchError("output did not preserve the original local ZIP region byte-for-byte")

    with zipfile.ZipFile(input_path, "r") as original, zipfile.ZipFile(output_path, "r") as patched:
        original_infos = {item.filename: item for item in original.infolist()}
        patched_infos = {item.filename: item for item in patched.infolist()}
        if len(patched.infolist()) != len(original.infolist()) + 1:
            raise PatchError("patched archive entry count is not original + 1")
        if len(patched_infos) != len(patched.infolist()):
            raise PatchError("patched archive contains duplicate central-directory names")
        if patched.read(GAME_ENTRY) != patched_game:
            raise PatchError("patched Game.gml payload failed verification")
        if patched.read(BRIDGE_ENTRY) != bridge:
            raise PatchError("DualScreenBridge.gml payload failed verification")
        if patched.getinfo(GAME_ENTRY).compress_type != zipfile.ZIP_STORED:
            raise PatchError("patched Game.gml is not stored like the exact source archive")
        if patched.getinfo(BRIDGE_ENTRY).compress_type != zipfile.ZIP_STORED:
            raise PatchError("DualScreenBridge.gml is not stored like the exact source archive")

        for name, before in original_infos.items():
            if name == GAME_ENTRY:
                continue
            after = patched_infos.get(name)
            if after is None:
                raise PatchError(f"untouched entry disappeared: {name}")
            if (before.CRC, before.file_size, before.compress_size) != (
                after.CRC,
                after.file_size,
                after.compress_size,
            ):
                raise PatchError(f"untouched entry metadata changed: {name}")

        if full_crc:
            bad_entry = patched.testzip()
            if bad_entry is not None:
                raise PatchError(f"CRC validation failed for {bad_entry}")

    return {
        "patched_archive_sha256": sha256_file(output_path),
        "patched_archive_size": output_path.stat().st_size,
        "full_crc_verified": full_crc,
    }


def write_json_transactional(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(f".{path.name}.partial-{uuid.uuid4().hex}")
    try:
        with temp.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
    finally:
        if temp.exists():
            temp.unlink()


def execute(args: argparse.Namespace) -> int:
    script_root = Path(__file__).resolve().parent.parent
    input_path = Path(args.input).resolve()
    bridge_path = Path(args.bridge_source or script_root / "gml" / "DualScreenBridge.gml").resolve()

    (
        patched_game,
        bridge,
        _game_info,
        archive_comment,
        original_start_dir,
        archive_hash_bytes,
    ) = validate_input(input_path, bridge_path)
    archive_hash = archive_hash_bytes.decode("ascii")

    plan = {
        "tool_schema": PATCH_TOOL_SCHEMA,
        "mode": "dry-run" if args.dry_run else "build",
        "input": str(input_path),
        "input_sha256": archive_hash,
        "input_size": input_path.stat().st_size,
        "input_entries": EXPECTED_ENTRY_COUNT,
        "game_entry": GAME_ENTRY,
        "game_original_sha256": EXPECTED_GAME_SHA256,
        "game_patched_sha256": sha256_bytes(patched_game),
        "bridge_entry": BRIDGE_ENTRY,
        "bridge_sha256": sha256_bytes(bridge),
        "new_entry_compression": "stored",
        "anchors": [patch.name for patch in ANCHOR_PATCHES],
        "backup_required_before_install": True,
        "required_backup_name": f"assets.zip.fomds-original-{archive_hash[:16]}",
    }

    if args.dry_run:
        print(json.dumps(plan, indent=2, sort_keys=True))
        return 0

    if not args.output:
        raise PatchError("--output is required unless --dry-run is used")
    output_path = Path(args.output).resolve()
    if output_path == input_path:
        raise PatchError("output must not be the input archive")
    if output_path.exists() and not args.replace:
        raise PatchError(f"output already exists (pass --replace to replace it): {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_temp = output_path.with_name(f".{output_path.name}.partial-{uuid.uuid4().hex}")
    manifest_path = Path(args.manifest).resolve() if args.manifest else Path(str(output_path) + ".manifest.json")
    if manifest_path.exists() and not args.replace:
        raise PatchError(f"manifest already exists (pass --replace to replace it): {manifest_path}")

    try:
        layout = build_archive(
            input_path,
            output_temp,
            patched_game=patched_game,
            bridge=bridge,
            original_count=EXPECTED_ENTRY_COUNT,
            original_start_dir=original_start_dir,
            archive_comment=archive_comment,
        )
        verified = verify_output(
            input_path,
            output_temp,
            patched_game=patched_game,
            bridge=bridge,
            original_start_dir=original_start_dir,
            original_prefix_hash=str(layout["original_local_region_sha256"]),
            full_crc=not args.skip_full_crc,
        )

        manifest = {
            **plan,
            **layout,
            **verified,
            "output": str(output_path),
            "untouched_local_records_and_compressed_data_preserved": True,
            "untouched_central_records_preserved": True,
            "original_game_local_record_retained_unreferenced": True,
            "install_contract": {
                "source_sha256_must_equal": EXPECTED_ARCHIVE_SHA256,
                "create_verified_backup_before_replace": True,
                "never_overwrite_the_only_original": True,
                "install_by_same_directory_atomic_rename": True,
                "rollback_restores_the_verified_original": True,
            },
        }

        os.replace(output_temp, output_path)
        write_json_transactional(manifest_path, manifest)
        print(json.dumps(manifest, indent=2, sort_keys=True))
        return 0
    finally:
        if output_temp.exists():
            output_temp.unlink()


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--input", required=True, help="exact original v1.0.4 assets.zip")
    result.add_argument("--output", help="new patched ZIP; input is never modified")
    result.add_argument("--manifest", help="manifest path (default: OUTPUT.manifest.json)")
    result.add_argument("--bridge-source", help="override repository bridge source path")
    result.add_argument("--dry-run", action="store_true", help="validate identity/anchors without writing")
    result.add_argument("--replace", action="store_true", help="allow replacing an existing output")
    result.add_argument(
        "--skip-full-crc",
        action="store_true",
        help="skip the final all-entry decompression test (metadata/raw-region checks still run)",
    )
    return result


def main() -> int:
    try:
        return execute(parser().parse_args())
    except (PatchError, OSError, zipfile.BadZipFile) as error:
        print(f"patch-assets: ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
