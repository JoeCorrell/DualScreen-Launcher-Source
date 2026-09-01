#!/usr/bin/env python3
"""Extract one texture from a Chrome Engine RP6L resource pack as DDS.

The RP6L table layout follows Qivex/rpack-extract (MIT) and the texture-to-DDS
mapping follows hhrhhr's RP6L unpacker.  Keeping this tiny, targeted extractor
in-tree makes the Valheim-style build assets reproducible without shipping a
third-party executable or modifying the game's archives.
"""

from __future__ import annotations

import argparse
import pathlib
import struct
import zlib


HEADER = struct.Struct("<4s8I")
SECTION = struct.Struct("<4B4I")
PART = struct.Struct("<BBH3I")
FILE = struct.Struct("<4B2I")


def fourcc(value: bytes) -> int:
    return struct.unpack("<I", value)[0]


def dds_header(width: int, height: int, mips: int, texture_format: int) -> bytes:
    # Chrome texture formats used by the shipped Dead Island menu pack.
    formats = {
        2: None,  # BGRA8
        3: None,  # BGRA8
        14: b"DXT3",
        17: b"DXT1",
        18: b"DXT3",
        19: b"DXT5",
        33: b"DXT5",
    }
    if texture_format not in formats:
        raise ValueError(f"unsupported Chrome texture format {texture_format}")

    DDSD_CAPS = 0x1
    DDSD_HEIGHT = 0x2
    DDSD_WIDTH = 0x4
    DDSD_PITCH = 0x8
    DDSD_PIXELFORMAT = 0x1000
    DDSD_MIPMAPCOUNT = 0x20000
    DDSD_LINEARSIZE = 0x80000
    DDSCAPS_COMPLEX = 0x8
    DDSCAPS_TEXTURE = 0x1000
    DDSCAPS_MIPMAP = 0x400000
    DDPF_ALPHAPIXELS = 0x1
    DDPF_FOURCC = 0x4
    DDPF_RGB = 0x40

    compressed = formats[texture_format]
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT
    caps = DDSCAPS_TEXTURE
    if mips > 1:
        flags |= DDSD_MIPMAPCOUNT
        caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP

    if compressed is None:
        flags |= DDSD_PITCH
        pitch = width * 4
        pixel_format = (
            32,
            DDPF_RGB | DDPF_ALPHAPIXELS,
            0,
            32,
            0x00FF0000,
            0x0000FF00,
            0x000000FF,
            0xFF000000,
        )
    else:
        flags |= DDSD_LINEARSIZE
        block = 8 if compressed == b"DXT1" else 16
        pitch = block * ((width + 3) // 4) * ((height + 3) // 4)
        pixel_format = (32, DDPF_FOURCC, fourcc(compressed), 0, 0, 0, 0, 0)

    values = [
        fourcc(b"DDS "),
        124,
        flags,
        height,
        width,
        pitch,
        0,
        mips if mips > 1 else 0,
        *([0] * 11),
        *pixel_format,
        caps,
        0,
        0,
        0,
        0,
    ]
    assert len(values) == 32
    return struct.pack("<32I", *values)


def extract_texture(pack_path: pathlib.Path, wanted: str) -> tuple[str, bytes]:
    with pack_path.open("rb") as stream:
        signature, version, compression, part_count, section_count, file_count, \
            names_size, names_count, block_size = HEADER.unpack(stream.read(HEADER.size))
        if signature != b"RP6L":
            raise ValueError("not an RP6L resource pack")
        del compression, names_count, block_size
        multiplier = 16 if version == 4 else 1

        sections = [SECTION.unpack(stream.read(SECTION.size))
                    for _ in range(section_count)]
        parts = [PART.unpack(stream.read(PART.size)) for _ in range(part_count)]
        files = [FILE.unpack(stream.read(FILE.size)) for _ in range(file_count)]
        name_offsets = struct.unpack(f"<{file_count}I", stream.read(file_count * 4))
        names = stream.read(names_size)

        section_data: list[bytes | None] = [None] * section_count
        for index, section in enumerate(sections):
            _filetype, _a, _b, _c, offset, unpacked, packed, _unknown = section
            if packed:
                stream.seek(offset * multiplier)
                compressed_data = stream.read(packed)
                inflated = zlib.decompress(compressed_data)
                if len(inflated) != unpacked:
                    raise ValueError(f"section {index} inflated to the wrong size")
                section_data[index] = inflated

        wanted_lower = wanted.lower()
        for index, file_info in enumerate(files):
            part_amount, _unknown1, filetype, _unknown2, _file_index, first_part = file_info
            start = name_offsets[index]
            end = names.find(b"\0", start)
            if end < 0:
                raise ValueError("unterminated resource filename")
            filename = names[start:end].decode("utf-8", errors="replace")
            if filetype != 0x20 or filename.lower() != wanted_lower:
                continue

            chunks: list[bytes] = []
            for part_index in range(first_part, first_part + part_amount):
                section_index, _p1, _file, offset, size, _p2 = parts[part_index]
                cached = section_data[section_index]
                if cached is not None:
                    chunks.append(cached[offset:offset + size])
                else:
                    section = sections[section_index]
                    stream.seek((section[4] + offset) * multiplier)
                    chunks.append(stream.read(size))
            raw = b"".join(chunks)
            if len(raw) < 151:
                raise ValueError(f"texture {filename} is truncated")
            width, height = struct.unpack_from("<HH", raw, 0)
            mips = struct.unpack_from("<H", raw, 8)[0]
            texture_format = struct.unpack_from("<I", raw, 12)[0]
            # Chrome's serialized texture prefix is 151 bytes. The original
            # Lua extractor uses data:sub(152), whose index is one-based.
            return filename, dds_header(width, height, mips, texture_format) + raw[151:]

    raise FileNotFoundError(f"texture '{wanted}' was not found in {pack_path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("pack", type=pathlib.Path)
    parser.add_argument("texture")
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()
    name, data = extract_texture(args.pack, args.texture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"extracted {name} -> {args.output} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
