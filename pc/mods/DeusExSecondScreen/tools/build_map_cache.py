#!/usr/bin/env python3
"""Build a local, floor-aware DXHR map raster cache from the user's game.

This tool deliberately keeps derived game artwork under ``build/``.  It reads
the Director's Cut BIGFILE archives without changing them, follows each
dtp::Map -> gfxres -> CFX/SWF reference, asks a pinned FFDec installation to
rasterize each individual floor, then repacks the PNG into a tiny zlib-backed
format understood by MapRasterCache.cpp.

Only Python's standard library is required.  FFDec itself is acquired and
hash-verified by BuildMapCache.ps1 and is not redistributed with the mod.
"""

from __future__ import annotations

import argparse
import binascii
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib


SPEC_MASK = 0xBFFF0001
SHARD_SPAN = 0x7FF00000
MAP_MAGIC = b"DXHRMAP\0"
MAP_VERSION = 1
MAP_FORMAT_RGBA8 = 1
MAP_HEADER = struct.Struct("<8sIIIiIIIIIIIIII")

# All dtp::Map records shipped by the PC Director's Cut build.  The map movie
# references are resolved from the DRM fixup tables rather than being guessed
# from nearby DTP IDs.  sha_tym_pool_room (81687) intentionally shares a movie
# with part of the TYM map set.
MAP_DTP_IDS = (
    859, 8299,
    10769, 10770, 10771, 10772, 10773, 10774, 10775, 10776,
    15452, 19468, 21380, 22029, 23751, 25721,
    27505, 27506, 29314, 30103, 30537,
    35737, 37740, 40390, 42150, 42337, 43118, 43596,
    43706, 43707, 43708, 43709, 46149,
    49334, 49498, 49880,
    73897, 73898, 73899, 73900, 73901, 73902,
    74640, 75406, 76038, 76713, 76895, 80255, 81290,
    81373, 81374, 81375, 81687,
    84648, 86432,
)


def align16(value: int) -> int:
    return (value + 15) & ~15


_FLIP_BYTE = bytes(
    int(f"{value:08b}"[::-1], 2) for value in range(256)
)


def _flip_word(value: int) -> int:
    return (
        _FLIP_BYTE[(value >> 24) & 0xFF]
        | (_FLIP_BYTE[(value >> 16) & 0xFF] << 8)
        | (_FLIP_BYTE[(value >> 8) & 0xFF] << 16)
        | (_FLIP_BYTE[value & 0xFF] << 24)
    )


def dxhr_path_hash(path: bytes) -> int:
    """Return the reflected CRC used by DXHR's BIGFILE path table."""
    return _flip_word(binascii.crc32(bytes(_FLIP_BYTE[b] for b in path)))


class BigFileEntry:
    __slots__ = ("offset", "size", "language", "path")

    def __init__(self, offset: int, size: int, language: int,
                 path: str = "") -> None:
        self.offset = offset
        self.size = size
        self.language = language
        self.path = path


class BigFile:
    def __init__(self, game: Path) -> None:
        index_path = game / "BIGFILE.000"
        if not index_path.is_file():
            raise RuntimeError(f"BIGFILE.000 was not found under {game}")

        self._shards = []
        for index in range(1000):
            path = game / f"BIGFILE.{index:03d}"
            if not path.is_file():
                break
            self._shards.append(path.open("rb"))
        if not self._shards:
            raise RuntimeError("no BIGFILE shards could be opened")

        self.entries: dict[int, list[BigFileEntry]] = {}
        first = self._shards[0]
        first.seek(68)
        count_raw = first.read(4)
        if len(count_raw) != 4:
            raise RuntimeError("BIGFILE index header is truncated")
        count = struct.unpack("<I", count_raw)[0]
        if count == 0 or count > 1_000_000:
            raise RuntimeError(f"implausible BIGFILE entry count {count}")
        hashes = first.read(count * 4)
        records = first.read(count * 16)
        if len(hashes) != count * 4 or len(records) != count * 16:
            raise RuntimeError("BIGFILE index tables are truncated")
        for index in range(count):
            path_hash = struct.unpack_from("<I", hashes, index * 4)[0]
            size, block_offset, language, zero = struct.unpack_from(
                "<IIII", records, index * 16
            )
            if zero != 0:
                raise RuntimeError("BIGFILE entry has an invalid reserved field")
            entry = BigFileEntry(block_offset * 2048, size, language)
            self.entries.setdefault(path_hash, []).append(entry)

    def close(self) -> None:
        for shard in self._shards:
            shard.close()
        self._shards.clear()

    def __enter__(self) -> "BigFile":
        return self

    def __exit__(self, *_args: object) -> None:
        self.close()

    def read_at(self, offset: int, size: int) -> bytes:
        if size < 0 or offset < 0:
            raise RuntimeError("negative BIGFILE read")
        output = bytearray()
        while size:
            shard_number, local_offset = divmod(offset, SHARD_SPAN)
            if shard_number >= len(self._shards):
                raise RuntimeError("BIGFILE read points beyond the final shard")
            take = min(size, SHARD_SPAN - local_offset)
            shard = self._shards[shard_number]
            shard.seek(local_offset)
            data = shard.read(take)
            if len(data) != take:
                raise RuntimeError("BIGFILE shard data is truncated")
            output.extend(data)
            offset += take
            size -= take
        return bytes(output)

    def find(self, path: str) -> BigFileEntry | None:
        encoded = path.encode("ascii")
        for entry in self.entries.get(dxhr_path_hash(encoded), ()):
            if entry.language & SPEC_MASK == SPEC_MASK:
                entry.path = path
                return entry
        return None

    def read_entry(self, entry: BigFileEntry) -> bytes:
        return self.read_at(entry.offset, entry.size)

    def decompressed_prefix(self, entry: BigFileEntry) -> bytes:
        """Read only enough decompressed bytes to cover a DRM section table."""
        first = self.read_at(entry.offset, min(entry.size, 16))
        if first[:4] != b"CDRM":
            prefix = self.read_at(entry.offset, min(entry.size, 32))
            if len(prefix) < 32:
                return prefix
            section_count = struct.unpack_from("<I", prefix, 24)[0]
            wanted = 32 + section_count * 20
            return self.read_at(entry.offset, min(entry.size, wanted))

        if len(first) != 16:
            raise RuntimeError(f"{entry.path}: truncated CDRM header")
        magic, _version, chunk_count, _padding = struct.unpack("<IIII", first)
        if magic != 0x4D524443 or chunk_count == 0 or chunk_count > 1_000_000:
            raise RuntimeError(f"{entry.path}: invalid CDRM chunk table")
        table_bytes = 16 + chunk_count * 8
        table = self.read_at(entry.offset, table_bytes)
        packed_cursor = align16(table_bytes)
        output = bytearray()
        wanted = 32
        for index in range(chunk_count):
            info, packed_size = struct.unpack_from("<II", table, 16 + index * 8)
            unpacked_size = info >> 8
            compression = info & 0xFF
            if packed_size > entry.size or packed_cursor + packed_size > entry.size:
                raise RuntimeError(f"{entry.path}: CDRM chunk points outside entry")
            packed = self.read_at(entry.offset + packed_cursor, packed_size)
            if compression == 1:
                chunk = packed
            elif compression == 2:
                chunk = zlib.decompress(packed)
            else:
                raise RuntimeError(
                    f"{entry.path}: unsupported CDRM compression {compression}"
                )
            if len(chunk) != unpacked_size:
                raise RuntimeError(f"{entry.path}: CDRM chunk size mismatch")
            output.extend(chunk)
            output.extend(b"\0" * ((-len(chunk)) & 15))
            packed_cursor += align16(packed_size)
            if len(output) >= 32 and wanted == 32:
                version, = struct.unpack_from("<I", output, 0)
                if version not in (19, 21):
                    return bytes(output)
                section_count = struct.unpack_from("<I", output, 24)[0]
                if section_count > 1_000_000:
                    raise RuntimeError(f"{entry.path}: implausible DRM section count")
                wanted = 32 + section_count * 20
            if len(output) >= wanted:
                return bytes(output[:wanted])
        return bytes(output)


def decompress_cdrm(data: bytes, source: str) -> bytes:
    if data[:4] != b"CDRM":
        return data
    if len(data) < 16:
        raise RuntimeError(f"{source}: truncated CDRM header")
    magic, _version, chunk_count, _padding = struct.unpack_from("<IIII", data)
    if magic != 0x4D524443 or chunk_count == 0 or chunk_count > 1_000_000:
        raise RuntimeError(f"{source}: invalid CDRM header")
    table_end = 16 + chunk_count * 8
    if table_end > len(data):
        raise RuntimeError(f"{source}: truncated CDRM table")
    cursor = align16(table_end)
    parts: list[bytes] = []
    for index in range(chunk_count):
        info, packed_size = struct.unpack_from("<II", data, 16 + index * 8)
        unpacked_size = info >> 8
        compression = info & 0xFF
        if cursor + packed_size > len(data):
            raise RuntimeError(f"{source}: CDRM chunk points outside the file")
        packed = data[cursor:cursor + packed_size]
        if compression == 1:
            chunk = packed
        elif compression == 2:
            chunk = zlib.decompress(packed)
        else:
            raise RuntimeError(
                f"{source}: unsupported CDRM compression {compression}"
            )
        if len(chunk) != unpacked_size:
            raise RuntimeError(f"{source}: decompressed CDRM size mismatch")
        parts.append(chunk)
        parts.append(b"\0" * ((-len(chunk)) & 15))
        cursor += align16(packed_size)
    return b"".join(parts)


class Section:
    __slots__ = (
        "index", "type_id", "subtype_id", "section_id", "language",
        "payload", "fixups",
    )

    def __init__(self, index: int, type_id: int, subtype_id: int,
                 section_id: int, language: int, payload: bytes) -> None:
        self.index = index
        self.type_id = type_id
        self.subtype_id = subtype_id
        self.section_id = section_id
        self.language = language
        self.payload = payload
        self.fixups: dict[int, tuple[int, int | tuple[int, int], int]] = {}


def parse_fixups(raw: bytes, section: Section,
                 id_to_index: dict[tuple[int, int], int]) -> None:
    if not raw:
        return
    if len(raw) < 20:
        raise RuntimeError("truncated DRM fixup header")
    count0, count1, count2, count3, count4 = struct.unpack_from("<IIIII", raw)
    if count3:
        raise RuntimeError("unsupported nonzero DRM type-3 fixup count")
    cursor = 20
    needed = cursor + (count0 + count1) * 8 + (count2 + count4) * 4
    if needed > len(raw):
        raise RuntimeError("truncated DRM fixup table")
    for _ in range(count0):
        value, = struct.unpack_from("<Q", raw, cursor)
        cursor += 8
        patchsite = value & 0xFFFFFFFF
        target_offset = value >> 32
        section.fixups[patchsite] = (0, section.index, target_offset)
    for _ in range(count1):
        value, = struct.unpack_from("<Q", raw, cursor)
        cursor += 8
        target_index = value & 0x3FFF
        patchsite = (value >> 12) & 0xFFFFFF
        target_offset = value >> 38
        section.fixups[patchsite] = (1, target_index, target_offset)
    for kind, count in ((2, count2), (4, count4)):
        for _ in range(count):
            value, = struct.unpack_from("<I", raw, cursor)
            cursor += 4
            patchsite = (value & 0x01FFFFFF) * 4
            target_type = value >> 25
            if patchsite + 4 > len(section.payload):
                raise RuntimeError("DRM fixup patchsite is outside its section")
            target_id, = struct.unpack_from("<I", section.payload, patchsite)
            target: int | tuple[int, int] = id_to_index.get(
                (target_type, target_id), (target_type, target_id)
            )
            section.fixups[patchsite] = (kind, target, 0)


def parse_drm(data: bytes, source: str) -> list[Section]:
    data = decompress_cdrm(data, source)
    if len(data) < 32:
        raise RuntimeError(f"{source}: DRM header is truncated")
    (version, drm_deps_size, object_deps_size, _unknown0c, _unknown10,
     _flags, section_count, _root) = struct.unpack_from("<IIIIIIII", data)
    if version not in (19, 21):
        raise RuntimeError(f"{source}: unsupported DRM version {version}")
    table_end = 32 + section_count * 20
    if section_count > 1_000_000 or table_end > len(data):
        raise RuntimeError(f"{source}: invalid DRM section table")

    descriptors = []
    id_to_index: dict[tuple[int, int], int] = {}
    for index in range(section_count):
        descriptor = struct.unpack_from("<IBBHIII", data, 32 + index * 20)
        payload_size, type_id, _unknown05, _unknown06, flags, sid, language = descriptor
        subtype_id = (flags >> 1) & 0x7F
        reloc_size = flags >> 8
        descriptors.append(
            (payload_size, type_id, subtype_id, sid, language, reloc_size)
        )
        if language & SPEC_MASK == SPEC_MASK:
            id_to_index[type_id, sid] = index

    cursor = align16(table_end + object_deps_size + drm_deps_size)
    sections: list[Section] = []
    fixup_blobs: list[bytes] = []
    for index, descriptor in enumerate(descriptors):
        payload_size, type_id, subtype_id, sid, language, reloc_size = descriptor
        if cursor + reloc_size > len(data):
            raise RuntimeError(f"{source}: section fixups are truncated")
        fixups = data[cursor:cursor + reloc_size]
        cursor = align16(cursor + reloc_size)
        if cursor + payload_size > len(data):
            raise RuntimeError(f"{source}: section payload is truncated")
        payload = data[cursor:cursor + payload_size]
        cursor = align16(cursor + payload_size)
        sections.append(Section(index, type_id, subtype_id, sid, language,
                                payload))
        fixup_blobs.append(fixups)
    for section, raw in zip(sections, fixup_blobs):
        parse_fixups(raw, section, id_to_index)
    return sections


def referenced_sections(section: Section,
                        sections: list[Section]) -> list[Section]:
    result = []
    seen = set()
    for _offset, (_kind, target, _target_offset) in sorted(section.fixups.items()):
        if not isinstance(target, int) or target < 0 or target >= len(sections):
            continue
        if target not in seen:
            result.append(sections[target])
            seen.add(target)
    return result


def find_map_swf(sections: list[Section], map_id: int) -> tuple[int, bytes]:
    starts = [
        section for section in sections
        if section.type_id == 7 and section.section_id == map_id
        and section.language & SPEC_MASK == SPEC_MASK
    ]
    if not starts:
        raise RuntimeError(f"DTP map {map_id} is absent from selected DRM")

    # The retail chain is Map -> subtype-41 gfxres -> CFX movie.  A bounded
    # breadth-first traversal tolerates the few duplicate/wrapper resources in
    # Director's Cut without accepting an unrelated movie elsewhere in a DRM.
    queue: list[tuple[Section, int]] = [(starts[0], 0)]
    visited = set()
    while queue:
        section, depth = queue.pop(0)
        if section.index in visited or depth > 3:
            continue
        visited.add(section.index)
        if (section.type_id == 7 and len(section.payload) >= 12
                and section.payload[4:7] == b"CFX"):
            return section.section_id, decode_cfx(section.payload)
        for child in referenced_sections(section, sections):
            if depth == 0 and child.subtype_id != 41:
                continue
            queue.append((child, depth + 1))
    raise RuntimeError(f"DTP map {map_id} has no reachable CFX map movie")


def decode_cfx(payload: bytes) -> bytes:
    if len(payload) < 12 or payload[4:7] != b"CFX":
        raise RuntimeError("section is not a CFX movie")
    version = payload[7]
    declared_size, = struct.unpack_from("<I", payload, 8)
    body = zlib.decompress(payload[12:])
    if declared_size != len(body) + 8:
        raise RuntimeError("CFX movie has an inconsistent uncompressed size")
    swf = b"FWS" + bytes((version,)) + struct.pack("<I", declared_size) + body
    if len(swf) != declared_size:
        raise RuntimeError("decoded SWF size mismatch")
    return swf


def parse_section_ids(prefix: bytes) -> set[int]:
    if len(prefix) < 32:
        return set()
    version, = struct.unpack_from("<I", prefix)
    if version not in (19, 21):
        return set()
    section_count, = struct.unpack_from("<I", prefix, 24)
    if section_count > 1_000_000 or 32 + section_count * 20 > len(prefix):
        return set()
    result = set()
    for index in range(section_count):
        _size, type_id, _u5, _u6, _flags, sid, language = struct.unpack_from(
            "<IBBHIII", prefix, 32 + index * 20
        )
        if type_id == 7 and language & SPEC_MASK == SPEC_MASK:
            result.add(sid)
    return result


ROOT_FLOOR_RE = re.compile(
    r'^[0-9a-fA-F]+: {2}(\d+)\. PlaceObject\d* .*nm: "(sublevel|level)(\d+)_mc"'
)
SVG_MATRIX_RE = re.compile(r"matrix\(([^)]*)\)")


def run(command: list[str], *, capture: bool = False) -> str:
    completed = subprocess.run(
        command,
        stdout=subprocess.PIPE if capture else subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if completed.returncode != 0:
        details = completed.stdout or ""
        raise RuntimeError(
            f"command failed ({completed.returncode}): "
            + subprocess.list2cmdline(command)
            + (f"\n{details}" if details else "")
        )
    return completed.stdout or ""


def ffdec_command(java: Path, ffdec: Path, *arguments: str) -> list[str]:
    return [str(java), "-Djava.awt.headless=true", "-jar", str(ffdec),
            *arguments]


def enumerate_floors(java: Path, ffdec: Path,
                     swf: Path) -> list[tuple[int, int]]:
    dump = run(ffdec_command(java, ffdec, "-dumpSWF", str(swf)), capture=True)
    floors: list[tuple[int, int]] = []
    seen = set()
    for line in dump.splitlines():
        match = ROOT_FLOOR_RE.search(line)
        if not match:
            continue
        tag_index = int(match.group(1))
        floor = int(match.group(3))
        if match.group(2) == "sublevel":
            floor = -floor
        if floor in seen:
            raise RuntimeError(f"{swf.name}: duplicate floor instance {floor}")
        floors.append((floor, tag_index))
        seen.add(floor)
    if not floors:
        # A few extremely small map movies may not use the MapLevel class.  A
        # full-frame fallback remains exact for those single-layer assets.
        floors.append((0, -1))
    floors.sort(key=lambda item: item[0])
    return floors


def export_reference_svg(java: Path, ffdec: Path, swf: Path,
                         work: Path) -> Path:
    """Export vector metadata without clipping it to the nominal SWF stage."""
    out = work / "reference_svg"
    out.mkdir(parents=True, exist_ok=True)
    run(ffdec_command(
        java, ffdec,
        "-format", "frame:svg",
        "-select", "1",
        "-onerror", "abort",
        "-export", "frame", str(out), str(swf),
    ))
    svg = out / "1.svg"
    if not svg.is_file() or svg.stat().st_size < 128:
        raise RuntimeError(f"FFDec did not create SVG metadata for {swf.name}")
    return svg


def _svg_number(tag: str, attribute: str) -> float:
    match = re.search(
        rf'\b{re.escape(attribute)}="([-+0-9.eE]+)(?:px)?"', tag
    )
    if not match:
        raise RuntimeError(f"signingsHolder has no {attribute} attribute")
    value = float(match.group(1))
    if not math.isfinite(value):
        raise RuntimeError(f"signingsHolder has a non-finite {attribute}")
    return value


def read_signings_bounds(svg: Path) -> tuple[int, int, int, int]:
    """Return the authored signing reference rectangle in SWF twips.

    Vanilla places its player/objective/actor signings at normalized x/y times
    signingsHolder._width/_height.  Making this exact rectangle the temporary
    export stage therefore gives the runtime raster a direct 0..1 coordinate
    system without a second offset table.
    """
    text = svg.read_text(encoding="utf-8")
    match = re.search(r'<use\b[^>]*\bid="signingsHolder_mc"[^>]*/?>', text)
    if not match:
        raise RuntimeError(f"{svg}: signingsHolder_mc was not found")
    tag = match.group(0)
    width = _svg_number(tag, "width")
    height = _svg_number(tag, "height")
    transform = re.search(r'\btransform="([^"]+)"', tag)
    matrix = SVG_MATRIX_RE.fullmatch(transform.group(1)) if transform else None
    if not matrix:
        raise RuntimeError(f"{svg}: unsupported signingsHolder transform")
    values = [float(value.strip()) for value in matrix.group(1).split(",")]
    if len(values) != 6 or not all(math.isfinite(value) for value in values):
        raise RuntimeError(f"{svg}: malformed signingsHolder matrix")
    scale_x, skew_y, skew_x, scale_y, translate_x, translate_y = values
    # Every shipped Director's Cut map is axis aligned.  Refuse a surprising
    # build instead of silently producing a raster whose live markers drift.
    if (abs(skew_x) > 1e-5 or abs(skew_y) > 1e-5 or
            scale_x <= 0.0 or scale_y <= 0.0):
        raise RuntimeError(f"{svg}: signingsHolder is rotated or mirrored")
    x_min = math.floor(translate_x * 20.0)
    y_min = math.floor(translate_y * 20.0)
    x_max = math.ceil((translate_x + scale_x * width) * 20.0)
    y_max = math.ceil((translate_y + scale_y * height) * 20.0)
    if x_max <= x_min or y_max <= y_min:
        raise RuntimeError(f"{svg}: empty signingsHolder bounds")
    return x_min, x_max, y_min, y_max


def _signed_bit_count(value: int) -> int:
    return value.bit_length() + 1 if value >= 0 else (~value).bit_length() + 1


def encode_swf_rect(bounds: tuple[int, int, int, int]) -> bytes:
    bit_count = max(_signed_bit_count(value) for value in bounds)
    if bit_count > 31:
        raise RuntimeError("SWF stage rectangle is too large")
    bits = [(bit_count >> (4 - index)) & 1 for index in range(5)]
    for value in bounds:
        encoded = value & ((1 << bit_count) - 1)
        bits.extend(
            (encoded >> (bit_count - 1 - index)) & 1
            for index in range(bit_count)
        )
    bits.extend(0 for _ in range((-len(bits)) & 7))
    return bytes(
        sum(bits[offset + bit] << (7 - bit) for bit in range(8))
        for offset in range(0, len(bits), 8)
    )


def prepare_render_swf(source: bytes, bounds: tuple[int, int, int, int],
                       output: Path) -> int:
    """Expand the stage and neutralize Scaleform-only external signings.

    GFx custom tag 1009 references TGA marker/signing images which FFDec cannot
    resolve and paints as solid red placeholders.  They are not structural map
    geometry; the live UI draws those signings itself.  Replacing each custom
    definition with a transparent standard bitmap makes the offline export
    deterministic while preserving every tag index used for floor isolation.
    """
    if len(source) < 16 or source[:3] != b"FWS":
        raise RuntimeError("map movie must be an uncompressed FWS")
    old_rect_bits = source[8] >> 3
    old_rect_bytes = (5 + old_rect_bits * 4 + 7) // 8
    tag_start = 8 + old_rect_bytes + 4  # frame rate + frame count
    if tag_start > len(source):
        raise RuntimeError("truncated SWF header")

    result = bytearray(source[:8])
    result.extend(encode_swf_rect(bounds))
    result.extend(source[8 + old_rect_bytes:tag_start])
    cursor = tag_start
    stripped = 0
    saw_end = False
    while cursor + 2 <= len(source):
        record, = struct.unpack_from("<H", source, cursor)
        cursor += 2
        code = record >> 6
        length = record & 63
        if length == 63:
            if cursor + 4 > len(source):
                raise RuntimeError("truncated SWF long tag length")
            length, = struct.unpack_from("<I", source, cursor)
            cursor += 4
        if cursor + length > len(source):
            raise RuntimeError("SWF tag points beyond the movie")
        body = source[cursor:cursor + length]
        cursor += length
        if code == 1009:
            if len(body) < 2:
                raise RuntimeError("truncated GFx external-image tag")
            character_id, = struct.unpack_from("<H", body)
            # DefineBitsLossless2: one fully transparent ARGB pixel.
            body = struct.pack("<HBHH", character_id, 5, 1, 1)
            body += zlib.compress(b"\0\0\0\0", 9)
            code = 36
            stripped += 1
        short_length = len(body) if len(body) < 63 else 63
        result.extend(struct.pack("<H", (code << 6) | short_length))
        if short_length == 63:
            result.extend(struct.pack("<I", len(body)))
        result.extend(body)
        if code == 0:
            saw_end = True
            if cursor < len(source):
                result.extend(source[cursor:])
            break
    if not saw_end:
        raise RuntimeError("SWF has no End tag")
    struct.pack_into("<I", result, 4, len(result))
    output.write_bytes(result)
    return stripped


def export_floor_png(java: Path, ffdec: Path, swf: Path,
                     floors: list[tuple[int, int]], floor: int,
                     work: Path, zoom: float) -> Path:
    source = swf
    tag_for_floor = dict(floors)[floor]
    removable = [tag for _floor, tag in floors
                 if tag >= 0 and tag != tag_for_floor]
    if removable:
        source = work / f"floor_{floor}.swf"
        run(ffdec_command(
            java, ffdec, "-remove", str(swf), str(source),
            *(str(tag) for tag in removable)
        ))
    out = work / f"png_floor_{floor}"
    out.mkdir(parents=True, exist_ok=True)
    run(ffdec_command(
        java, ffdec,
        "-ignorebackground",
        "-zoom", format(zoom, ".6g"),
        "-format", "frame:png",
        "-select", "1",
        "-onerror", "abort",
        "-export", "frame", str(out), str(source),
    ))
    png = out / "1.png"
    if not png.is_file() or png.stat().st_size < 64:
        raise RuntimeError(f"FFDec did not create a usable PNG for floor {floor}")
    return png


def paeth(left: int, up: int, upper_left: int) -> int:
    estimate = left + up - upper_left
    left_distance = abs(estimate - left)
    up_distance = abs(estimate - up)
    corner_distance = abs(estimate - upper_left)
    if left_distance <= up_distance and left_distance <= corner_distance:
        return left
    if up_distance <= corner_distance:
        return up
    return upper_left


def decode_ffdec_png(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError(f"{path}: invalid PNG signature")
    cursor = 8
    width = height = 0
    compressed = bytearray()
    saw_end = False
    while cursor + 12 <= len(data):
        length, = struct.unpack_from(">I", data, cursor)
        chunk_type = data[cursor + 4:cursor + 8]
        begin = cursor + 8
        end = begin + length
        if end + 4 > len(data):
            raise RuntimeError(f"{path}: truncated PNG chunk")
        chunk = data[begin:end]
        expected_crc, = struct.unpack_from(">I", data, end)
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(chunk, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            raise RuntimeError(f"{path}: PNG chunk CRC mismatch")
        if chunk_type == b"IHDR":
            if length != 13:
                raise RuntimeError(f"{path}: invalid PNG IHDR")
            width, height, depth, color, compression, filtering, interlace = \
                struct.unpack(">IIBBBBB", chunk)
            if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise RuntimeError(
                    f"{path}: expected FFDec non-interlaced RGBA8 PNG, got "
                    f"depth={depth} color={color} interlace={interlace}"
                )
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            saw_end = True
            break
        cursor = end + 4
    if not saw_end or width <= 0 or height <= 0 or not compressed:
        raise RuntimeError(f"{path}: incomplete PNG")
    if width > 8192 or height > 8192:
        raise RuntimeError(f"{path}: unreasonable PNG dimensions")
    scanlines = zlib.decompress(bytes(compressed))
    row_bytes = width * 4
    expected = height * (row_bytes + 1)
    if len(scanlines) != expected:
        raise RuntimeError(f"{path}: unexpected decompressed PNG size")
    pixels = bytearray(width * height * 4)
    previous = bytearray(row_bytes)
    source = 0
    target = 0
    for _row in range(height):
        filter_type = scanlines[source]
        source += 1
        current = bytearray(scanlines[source:source + row_bytes])
        source += row_bytes
        # FFDec currently emits filter 0 for frame exports.  Keep the full PNG
        # decoder below for compatibility, but avoid millions of needless
        # Python byte operations on this overwhelmingly common fast path.
        if filter_type == 0:
            pixels[target:target + row_bytes] = current
            target += row_bytes
            previous = current
            continue
        for column in range(row_bytes):
            left = current[column - 4] if column >= 4 else 0
            up = previous[column]
            upper_left = previous[column - 4] if column >= 4 else 0
            if filter_type == 1:
                current[column] = (current[column] + left) & 0xFF
            elif filter_type == 2:
                current[column] = (current[column] + up) & 0xFF
            elif filter_type == 3:
                current[column] = (current[column] + ((left + up) >> 1)) & 0xFF
            elif filter_type == 4:
                current[column] = (
                    current[column] + paeth(left, up, upper_left)
                ) & 0xFF
            else:
                raise RuntimeError(f"{path}: unsupported PNG filter {filter_type}")
        pixels[target:target + row_bytes] = current
        target += row_bytes
        previous = current
    return width, height, bytes(pixels)


def write_map_cache(path: Path, map_id: int, floor: int, swf_id: int,
                    source_swf: bytes, width: int, height: int,
                    rgba: bytes) -> tuple[int, int, int]:
    raw_size = width * height * 4
    if len(rgba) != raw_size or raw_size > 512 * 1024 * 1024:
        raise RuntimeError("invalid raster byte count")
    raw_crc = zlib.crc32(rgba) & 0xFFFFFFFF
    source_crc = zlib.crc32(source_swf) & 0xFFFFFFFF
    packed = zlib.compress(rgba, 9)
    header = MAP_HEADER.pack(
        MAP_MAGIC, MAP_VERSION, MAP_HEADER.size, map_id, floor,
        width, height, MAP_FORMAT_RGBA8, raw_size, len(packed), raw_crc,
        swf_id, source_crc, 0, 0,
    )
    path.write_bytes(header + packed)
    return raw_crc, source_crc, len(header) + len(packed)


def collect_candidates(bigfile: BigFile, drm_paths: list[str],
                       wanted: set[int]) -> dict[int, list[BigFileEntry]]:
    jobs = []
    for path in drm_paths:
        entry = bigfile.find(path)
        if entry:
            jobs.append(entry)
    jobs.sort(key=lambda entry: entry.offset)
    result: dict[int, list[BigFileEntry]] = {map_id: [] for map_id in wanted}
    for number, entry in enumerate(jobs, 1):
        ids = parse_section_ids(bigfile.decompressed_prefix(entry))
        for map_id in ids & wanted:
            result[map_id].append(entry)
        if number % 2000 == 0:
            print(f"  indexed {number}/{len(jobs)} DRM files", flush=True)
    return result


def read_file_list(path: Path) -> list[str]:
    if not path.is_file():
        raise RuntimeError(f"Director's Cut file list was not found: {path}")
    paths = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if line.lower().endswith(".drm"):
            paths.append(line)
    if len(paths) < 1000:
        raise RuntimeError("file list does not look like the Director's Cut list")
    return paths


def locate_java(value: str) -> Path:
    path = Path(value)
    if path.is_file():
        return path
    found = shutil.which(value)
    if not found:
        raise RuntimeError(f"Java executable was not found: {value}")
    return Path(found)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--file-list", required=True, type=Path)
    parser.add_argument("--ffdec", required=True, type=Path)
    parser.add_argument("--java", default="java")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--zoom", type=float, default=4.0)
    parser.add_argument("--max-dimension", type=int, default=3072)
    parser.add_argument("--keep-png", action="store_true")
    parser.add_argument("--only-map", type=int, action="append", default=[])
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    game = args.game.resolve()
    file_list = args.file_list.resolve()
    ffdec = args.ffdec.resolve()
    java = locate_java(args.java)
    output = args.output.resolve()
    if not ffdec.is_file():
        raise RuntimeError(f"ffdec.jar was not found: {ffdec}")
    if not (0.25 <= args.zoom <= 12.0):
        raise RuntimeError("zoom must be between 0.25 and 12")
    if not (512 <= args.max_dimension <= 8192):
        raise RuntimeError("max dimension must be between 512 and 8192")
    wanted = set(args.only_map or MAP_DTP_IDS)
    unknown = wanted - set(MAP_DTP_IDS)
    if unknown:
        raise RuntimeError(f"unknown map DTP IDs requested: {sorted(unknown)}")

    drm_paths = read_file_list(file_list)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.parent / f"{output.name}.staging-{os.getpid()}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)
    started = time.monotonic()
    manifest_rows = []
    packed_count = 0

    try:
        print(f"Scanning {len(drm_paths)} DRM entries for {len(wanted)} maps...")
        with BigFile(game) as bigfile:
            candidates = collect_candidates(bigfile, drm_paths, wanted)
            missing = sorted(mid for mid, entries in candidates.items() if not entries)
            if missing:
                raise RuntimeError(
                    "map DTP records are missing; wrong game build or file list: "
                    + ", ".join(map(str, missing))
                )

            with tempfile.TemporaryDirectory(prefix="dxhr-map-cache-") as temp_name:
                temp = Path(temp_name)
                for map_number, map_id in enumerate(sorted(wanted), 1):
                    source_entry = None
                    swf_id = 0
                    swf = b""
                    errors = []
                    for entry in candidates[map_id]:
                        try:
                            sections = parse_drm(
                                bigfile.read_entry(entry), entry.path
                            )
                            swf_id, swf = find_map_swf(sections, map_id)
                            source_entry = entry
                            break
                        except Exception as error:  # try duplicated scene copies
                            errors.append(f"{entry.path}: {error}")
                    if source_entry is None:
                        raise RuntimeError(
                            f"could not resolve the movie for map {map_id}: "
                            + " | ".join(errors)
                        )

                    map_work = temp / str(map_id)
                    map_work.mkdir()
                    swf_path = map_work / f"map_{map_id}_{swf_id}.swf"
                    swf_path.write_bytes(swf)
                    floors = enumerate_floors(java, ffdec, swf_path)
                    svg = export_reference_svg(
                        java, ffdec, swf_path, map_work
                    )
                    bounds = read_signings_bounds(svg)
                    authored_width = (bounds[1] - bounds[0]) / 20.0
                    authored_height = (bounds[3] - bounds[2]) / 20.0
                    effective_zoom = min(
                        args.zoom,
                        (args.max_dimension - 1.0) /
                        max(authored_width, authored_height),
                    )
                    render_swf = map_work / "render.swf"
                    external_count = prepare_render_swf(
                        swf, bounds, render_swf
                    )
                    print(
                        f"[{map_number:02d}/{len(wanted):02d}] map {map_id} "
                        f"movie {swf_id}: floors "
                        + ", ".join(str(floor) for floor, _ in floors)
                        + f"; {authored_width:.2f}x{authored_height:.2f} "
                        + f"at {effective_zoom:.4g}x; stripped "
                        + f"{external_count} signing images",
                        flush=True,
                    )
                    for floor, _tag in floors:
                        png = export_floor_png(
                            java, ffdec, render_swf, floors, floor,
                            map_work, effective_zoom,
                        )
                        width, height, rgba = decode_ffdec_png(png)
                        if width > args.max_dimension or height > args.max_dimension:
                            raise RuntimeError(
                                f"map {map_id} floor {floor}: FFDec exceeded "
                                f"the {args.max_dimension}px output cap"
                            )
                        filename = f"map_{map_id}_f{floor}.dxm"
                        raw_crc, source_crc, file_bytes = write_map_cache(
                            staging / filename, map_id, floor, swf_id, swf,
                            width, height, rgba,
                        )
                        if args.keep_png:
                            png_dir = staging / "png"
                            png_dir.mkdir(exist_ok=True)
                            shutil.copy2(png, png_dir / f"map_{map_id}_f{floor}.png")
                        manifest_rows.append((
                            map_id, floor, swf_id, width, height,
                            f"{raw_crc:08x}", f"{source_crc:08x}",
                            file_bytes, f"{authored_width:.4f}",
                            f"{authored_height:.4f}",
                            f"{effective_zoom:.6g}", external_count,
                            filename, source_entry.path,
                        ))
                        packed_count += 1

        manifest_rows.sort(key=lambda row: (row[0], row[1]))
        with (staging / "manifest.tsv").open("w", encoding="utf-8", newline="\n") as file:
            file.write("# Deus Ex Human Revolution Director's Cut local map cache v1\n")
            file.write(
                "map_dtp_id\tfloor\tswf_dtp_id\twidth\theight\trgba_crc32\t"
                "swf_crc32\tfile_bytes\tauthored_width\tauthored_height\t"
                "render_zoom\texternal_signings_stripped\tfilename\t"
                "source_drm\n"
            )
            for row in manifest_rows:
                file.write("\t".join(map(str, row)) + "\n")
        (staging / "CACHE_COMPLETE").write_text(
            f"version={MAP_VERSION}\nmap_count={len(wanted)}\n"
            f"raster_count={packed_count}\nzoom={args.zoom:.6g}\n"
            f"max_dimension={args.max_dimension}\n",
            encoding="ascii",
        )

        backup = output.parent / f"{output.name}.old-{os.getpid()}"
        if backup.exists():
            shutil.rmtree(backup)
        if output.exists():
            output.rename(backup)
        staging.rename(output)
        if backup.exists():
            shutil.rmtree(backup)
    except Exception:
        if staging.exists():
            shutil.rmtree(staging)
        raise

    elapsed = time.monotonic() - started
    print(
        f"Built {packed_count} floor rasters for {len(wanted)} maps in "
        f"{elapsed:.1f}s: {output}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("cancelled", file=sys.stderr)
        raise SystemExit(130)
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
