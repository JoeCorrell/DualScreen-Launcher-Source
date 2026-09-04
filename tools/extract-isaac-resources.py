#!/usr/bin/env python3
"""Extract named resources from Binding of Isaac ARCH000 archives.

This is a focused, non-interactive extractor for launcher development.  It
uses the filename list shipped with the player's game, ignores unknown archive
entries, rejects unsafe output paths, and validates both expanded sizes and
the archive's native checksum before publishing a file.

The archive algorithms are adapted from Gibbed.Rebirth by Rick Gibbed, which
is distributed under the zlib license:
https://github.com/gibbed/Gibbed.Rebirth
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


U32_MASK = 0xFFFFFFFF
U64_MASK = 0xFFFFFFFFFFFFFFFF
ARCHIVE_HEADER = struct.Struct("<7sBIH")
ARCHIVE_ENTRY = struct.Struct("<IIIII")

# The expansion archives are overlays on the base category archives.  Apply
# them last so --overwrite reproduces the game's effective resource view.
ARCHIVE_ORDER = {
    "animations.a": 10,
    "config.a": 11,
    "fonts.a": 12,
    "graphics.a": 13,
    "rooms.a": 14,
    "music.a": 15,
    "sfx.a": 16,
    "afterbirth.a": 20,
    "afterbirthp.a": 30,
    "repentance.a": 40,
}


@dataclass(frozen=True)
class Entry:
    name_a: int
    name_b: int
    offset: int
    length: int
    checksum: int

    @property
    def name_hash(self) -> int:
        return self.name_a | (self.name_b << 32)


def u32(value: int) -> int:
    return value & U32_MASK


def name_hash(path: str) -> int:
    normalized = path.lower()
    hash_a = 5381
    hash_b = 0x5BB2220E
    for character in normalized:
        value = ord(character)
        hash_a = u32(hash_a * 33 + (value & 0xFF))
        hash_b = u32((hash_b ^ value) * 0x1000193)
    return hash_a | (hash_b << 32)


def archive_checksum(data: bytes) -> int:
    # The original implementation intentionally reuses the tail of this block
    # when a non-four-byte-aligned chunk follows an earlier chunk.
    checksum = 0xABABEB98
    block = bytearray(512)
    for offset in range(0, len(data), len(block)):
        chunk = data[offset : offset + len(block)]
        block[: len(chunk)] = chunk
        for word_offset in range(0, (len(chunk) + 3) & ~3, 4):
            word = struct.unpack_from("<I", block, word_offset)[0]
            checksum = u32((checksum >> 1) + word + (checksum << 31))
    return checksum


class BitReader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.position = 0
        self.remaining_bits = 0
        self.value = 0

    def read(self, bits: int) -> int:
        if bits <= 0 or bits > 32:
            raise ValueError(f"invalid bit count: {bits}")
        while self.remaining_bits < bits:
            if self.position >= len(self.data):
                raise EOFError("truncated LZW bitstream")
            self.value = u32((self.value << 8) | self.data[self.position])
            self.position += 1
            self.remaining_bits += 8
        self.remaining_bits -= bits
        mask = U32_MASK if bits == 32 else (1 << bits) - 1
        return (self.value >> self.remaining_bits) & mask


class CodeDictionary:
    CAPACITY = 4096

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        self.items: list[tuple[int, int]] = [(-1, index) for index in range(256)]
        self.code_length = 8

    def add(self, value: int, previous: int) -> None:
        if len(self.items) >= self.CAPACITY:
            raise ValueError("LZW dictionary overflow")
        if previous < 0 or previous >= len(self.items):
            raise ValueError("invalid LZW dictionary predecessor")
        self.items.append((previous, value))

    def update_code_length(self) -> None:
        while len(self.items) >= 1 << self.code_length:
            self.code_length += 1

    def decode(self, index: int) -> tuple[bytes, int]:
        if index < 0 or index >= len(self.items):
            raise ValueError(f"invalid LZW dictionary index: {index}")
        output = bytearray()
        while index != -1:
            if len(output) >= self.CAPACITY:
                raise ValueError("LZW decode chain overflow")
            previous, value = self.items[index]
            output.append(value)
            index = previous
        output.reverse()
        return bytes(output), output[0]


def decompress_lzw(stream, length: int) -> bytes:
    dictionary = CodeDictionary()
    output = bytearray()
    while len(output) < length:
        block_length_data = stream.read(4)
        if len(block_length_data) != 4:
            raise EOFError("truncated LZW block header")
        block_length = struct.unpack("<I", block_length_data)[0]
        block = stream.read(block_length)
        if len(block) != block_length:
            raise EOFError("truncated LZW block")
        reader = BitReader(block)
        previous = reader.read(dictionary.code_length)
        line, last_value = dictionary.decode(previous)
        output.extend(line)

        while reader.position < len(block):
            if len(dictionary.items) + 1 >= dictionary.CAPACITY:
                dictionary.reset()
                previous = reader.read(dictionary.code_length)
                line, last_value = dictionary.decode(previous)
                output.extend(line)
                continue

            dictionary.update_code_length()
            index = reader.read(dictionary.code_length)
            if index == len(dictionary.items):
                dictionary.add(last_value, previous)
                line, last_value = dictionary.decode(index)
            else:
                if index > len(dictionary.items):
                    raise ValueError("invalid LZW forward reference")
                line, last_value = dictionary.decode(index)
                dictionary.add(last_value, previous)
            output.extend(line)
            previous = index

    if len(output) != length:
        raise ValueError(f"LZW size mismatch: expected {length}, got {len(output)}")
    return bytes(output)


class IsaacCipher:
    SIZE = 256
    MASK = (SIZE - 1) << 2

    def __init__(self, seed: list[int]) -> None:
        self.state = [0] * self.SIZE
        self.results = [u32(value) for value in seed]
        self.index = 0
        self.a = self.b = self.c = 0
        self.initialize()

    @staticmethod
    def mix(values: list[int]) -> list[int]:
        a, b, c, d, e, f, g, h = values
        a = u32(a ^ (b << 11)); d = u32(d + a); b = u32(b + c)
        b = u32(b ^ (c >> 2)); e = u32(e + b); c = u32(c + d)
        c = u32(c ^ (d << 8)); f = u32(f + c); d = u32(d + e)
        d = u32(d ^ (e >> 16)); g = u32(g + d); e = u32(e + f)
        e = u32(e ^ (f << 10)); h = u32(h + e); f = u32(f + g)
        f = u32(f ^ (g >> 4)); a = u32(a + f); g = u32(g + h)
        g = u32(g ^ (h << 8)); b = u32(b + g); h = u32(h + a)
        h = u32(h ^ (a >> 9)); c = u32(c + h); a = u32(a + b)
        return [a, b, c, d, e, f, g, h]

    def initialize(self) -> None:
        values = [0x9E3779B9] * 8
        for _ in range(4):
            values = self.mix(values)
        for offset in range(0, self.SIZE, 8):
            values = [u32(values[i] + self.results[offset + i]) for i in range(8)]
            values = self.mix(values)
            self.state[offset : offset + 8] = values
        for offset in range(0, self.SIZE, 8):
            values = [u32(values[i] + self.state[offset + i]) for i in range(8)]
            values = self.mix(values)
            self.state[offset : offset + 8] = values
        self.generate()

    def generate(self) -> None:
        self.c = u32(self.c + 1)
        self.b = u32(self.b + self.c)
        for index in range(self.SIZE):
            x = self.state[index]
            mode = index & 3
            if mode == 0:
                self.a = u32(self.a ^ (self.a << 13))
            elif mode == 1:
                self.a = u32(self.a ^ (self.a >> 6))
            elif mode == 2:
                self.a = u32(self.a ^ (self.a << 2))
            else:
                self.a = u32(self.a ^ (self.a >> 16))
            self.a = u32(self.a + self.state[(index + 128) & 255])
            y = u32(self.state[(x & self.MASK) >> 2] + self.a + self.b)
            self.state[index] = y
            self.b = u32(self.state[((y >> 8) & self.MASK) >> 2] + x)
            self.results[index] = self.b

    def value(self) -> int:
        result = self.results[self.index]
        self.index += 1
        if self.index >= self.SIZE:
            self.generate()
            self.index = 0
        return result


def entry_cipher(name_b: int) -> IsaacCipher:
    seed64 = name_b
    mixed = seed64 ^ ((seed64 ^ ((seed64 << 15) & U64_MASK)) << 8) ^ (seed64 >> 9)
    seed64 = ((seed64 << 32) | (mixed & U32_MASK)) & U64_MASK
    seed: list[int] = []
    for _ in range(256):
        part = u32((seed64 >> 27) ^ (seed64 >> 45))
        shift = seed64 >> 59
        seed.append(u32((part >> shift) | (part << ((-shift) & 31))))
        seed64 = (seed64 * 6364136223846793005 + 127) & U64_MASK
    return IsaacCipher(seed)


def decrypt_bogocrypt1(data: bytearray, key: int) -> int:
    if len(data) % 4:
        raise ValueError("Bogocrypt1 block is not four-byte aligned")
    for offset in range(0, len(data), 4):
        data[offset + 0] ^= key & 0xFF
        data[offset + 1] ^= (key >> 8) & 0xFF
        data[offset + 2] ^= (key >> 16) & 0xFF
        data[offset + 3] ^= (key >> 24) & 0xFF
        mode = key & 15
        if mode == 2:
            data[offset], data[offset + 3] = data[offset + 3], data[offset]
            data[offset + 1], data[offset + 2] = data[offset + 2], data[offset + 1]
        elif mode == 9:
            data[offset], data[offset + 1] = data[offset + 1], data[offset]
            data[offset + 2], data[offset + 3] = data[offset + 3], data[offset + 2]
        elif mode == 13:
            data[offset], data[offset + 2] = data[offset + 2], data[offset]
            data[offset + 1], data[offset + 3] = data[offset + 3], data[offset + 1]
        stage = u32((key ^ u32(key << 8)) >> 9)
        key = u32(key ^ stage ^ u32(key << 8) ^ u32((stage ^ key ^ u32(key << 8)) << 23))
    return key


BOGOCRYPT2_TABLE = bytes([
    0xB0,0x22,0x5D,0x47,0xD3,0x1B,0x59,0xD1,0x97,0x37,0xFB,0xD2,0xC0,0x31,0x86,0xFD,
    0xB5,0xDD,0xAC,0x4B,0xB4,0x01,0x15,0x52,0x01,0x0A,0x75,0x08,0x5D,0x50,0xFE,0x8C,
    0xA8,0xAF,0xD5,0xCB,0x09,0xEE,0xDC,0xCA,0x76,0xC4,0x3F,0x67,0xED,0x09,0x8F,0x64,
    0x43,0x56,0x30,0x1A,0x89,0x2E,0xDF,0xF9,0xE4,0xB8,0x13,0x1A,0xBE,0xAF,0xA4,0x02,
    0xFF,0x60,0x22,0xE0,0x0B,0xFF,0xAE,0xC9,0x7D,0xA0,0x27,0xF9,0x11,0x63,0x2F,0xE1,
    0x20,0x7E,0x51,0xD7,0x0E,0xCD,0xC4,0xD4,0xCF,0xCF,0xA1,0x88,0x99,0x42,0x96,0xCF,
    0xF0,0xC1,0x54,0xC3,0x36,0x94,0xC2,0x92,0xDC,0xB8,0x63,0xCA,0xCA,0x32,0x12,0x7A,
    0x3C,0x88,0x5F,0xD2,0x4B,0xC3,0xE4,0x69,0x74,0x32,0xBD,0x2C,0x42,0x28,0xB2,0x34,
])


def read_entry(stream, mode: int, entry: Entry) -> bytes:
    stream.seek(entry.offset)
    if mode == 0:
        key = (entry.name_b ^ 0xF9524287) | 1
        output = bytearray()
        remaining = entry.length
        while remaining > 0:
            padded = min(1024, (remaining + 3) & ~3)
            actual = min(1024, remaining)
            block = bytearray(stream.read(padded))
            if len(block) != padded:
                raise EOFError("truncated Bogocrypt1 entry")
            key = decrypt_bogocrypt1(block, key)
            output.extend(block[:actual])
            remaining -= padded
        return bytes(output)
    if mode == 1:
        return decompress_lzw(stream, entry.length)
    if mode == 2:
        output = bytearray(entry.length)
        output_offset = 0
        compressed = True
        last = False
        cipher: IsaacCipher | None = None
        while not last:
            header = stream.read(4)
            if len(header) != 4:
                raise EOFError("truncated MiniZ block header")
            flags = struct.unpack("<I", header)[0]
            block_length = flags & 0x7FFFFFFF
            last = bool(flags & 0x80000000)
            if block_length > 0x800:
                raise ValueError("oversized MiniZ block")
            block = bytearray(stream.read(block_length))
            if len(block) != block_length:
                raise EOFError("truncated MiniZ block")
            if not compressed or (not last and block_length == 1024):
                compressed = False
                if cipher is None:
                    cipher = entry_cipher(entry.name_b)
                seed = 0
                for index in range(len(block)):
                    if index & 3:
                        seed >>= 8
                    else:
                        seed = cipher.value()
                    block[index] ^= seed & 0xFF
                expanded = bytes(block)
            else:
                # Isaac's reader asks a raw-DEFLATE inflater for at most one
                # 1024-byte output page and does not require each archive block
                # to carry a formal end-of-stream marker.  ``zlib.decompress``
                # insists on that marker, whereas the streaming API mirrors the
                # game reader and still reports malformed bitstreams.
                inflater = zlib.decompressobj(-zlib.MAX_WBITS)
                expanded = inflater.decompress(
                    bytes(block),
                    min(entry.length - output_offset, 1024),
                )
            end = output_offset + len(expanded)
            if end > entry.length:
                raise ValueError("MiniZ output overflow")
            output[output_offset:end] = expanded
            output_offset = end
        if output_offset != entry.length:
            raise ValueError(f"MiniZ size mismatch: expected {entry.length}, got {output_offset}")
        return bytes(output)
    if mode == 5:
        output = bytearray()
        remaining = entry.length
        while remaining >= 4:
            size = min(1024, remaining)
            block = bytearray(stream.read(size))
            if len(block) != size:
                raise EOFError("truncated Bogocrypt2 entry")
            for index in range(size):
                block[index] ^= BOGOCRYPT2_TABLE[index & 0x7F]
            output.extend(block)
            remaining -= size
        if remaining:
            tail = stream.read(remaining)
            if len(tail) != remaining:
                raise EOFError("truncated Bogocrypt2 tail")
            output.extend(tail)
        return bytes(output)
    raise ValueError(f"unsupported ARCH000 compression mode {mode}")


def safe_destination(root: Path, resource_name: str) -> Path:
    relative = PurePosixPath(resource_name.replace("\\", "/"))
    if relative.is_absolute() or ".." in relative.parts or not relative.parts:
        raise ValueError(f"unsafe resource path: {resource_name!r}")
    destination = root.joinpath(*relative.parts)
    destination.resolve().relative_to(root.resolve())
    return destination


def load_names(path: Path, pattern: re.Pattern[str] | None) -> dict[int, str]:
    names: dict[int, str] = {}
    for line in path.read_text(encoding="utf-8-sig", errors="strict").splitlines():
        name = line.strip().replace("\\", "/")
        if not name or (pattern is not None and pattern.search(name) is None):
            continue
        names.setdefault(name_hash(name), name)
    return names


def extract_archive(
    archive_path: Path,
    output_root: Path,
    names: dict[int, str],
    overwrite: bool,
) -> tuple[int, int]:
    extracted = 0
    unknown = 0
    with archive_path.open("rb") as stream:
        header = stream.read(ARCHIVE_HEADER.size)
        if len(header) != ARCHIVE_HEADER.size:
            raise EOFError(f"truncated archive header: {archive_path}")
        signature, mode, table_offset, count = ARCHIVE_HEADER.unpack(header)
        if signature != b"ARCH000":
            raise ValueError(f"invalid archive signature: {archive_path}")
        stream.seek(table_offset)
        entries = [Entry(*ARCHIVE_ENTRY.unpack(stream.read(ARCHIVE_ENTRY.size))) for _ in range(count)]
        for entry in entries:
            resource_name = names.get(entry.name_hash)
            if resource_name is None:
                unknown += 1
                continue
            destination = safe_destination(output_root, resource_name)
            if destination.exists() and not overwrite:
                continue
            data = read_entry(stream, mode, entry)
            actual_checksum = archive_checksum(data)
            if actual_checksum != entry.checksum:
                raise ValueError(
                    f"checksum mismatch for {resource_name}: "
                    f"expected {entry.checksum:08x}, got {actual_checksum:08x}"
                )
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(destination.name + ".partial")
            temporary.write_bytes(data)
            temporary.replace(destination)
            extracted += 1
            print(f"{archive_path.name}: {resource_name} ({len(data)} bytes)")
    return extracted, unknown


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive_directory", type=Path)
    parser.add_argument("filelist", type=Path)
    parser.add_argument("output_directory", type=Path)
    parser.add_argument("--filter", help="case-insensitive regular expression applied to resource paths")
    parser.add_argument("--overwrite", action="store_true")
    arguments = parser.parse_args()

    pattern = re.compile(arguments.filter, re.IGNORECASE) if arguments.filter else None
    names = load_names(arguments.filelist, pattern)
    if not names:
        parser.error("the filename list and filter selected no resources")
    archives = sorted(
        arguments.archive_directory.glob("*.a"),
        key=lambda archive: (ARCHIVE_ORDER.get(archive.name.lower(), 17), archive.name.lower()),
    )
    if not archives:
        parser.error("no .a archives were found")

    arguments.output_directory.mkdir(parents=True, exist_ok=True)
    total_extracted = 0
    total_unknown = 0
    for archive in archives:
        extracted, unknown = extract_archive(
            archive,
            arguments.output_directory,
            names,
            arguments.overwrite,
        )
        total_extracted += extracted
        total_unknown += unknown
    print(
        f"Extracted {total_extracted} verified resources from {len(archives)} archives; "
        f"{total_unknown} unnamed entries ignored."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, EOFError, ValueError, zlib.error) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
