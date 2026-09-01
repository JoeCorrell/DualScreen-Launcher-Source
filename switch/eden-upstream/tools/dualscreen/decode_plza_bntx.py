#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import io
import struct
from pathlib import Path

from PIL import Image


FORMATS = {
    0x1A: ("BC1", 4, 4, 8),
    0x1B: ("BC2", 4, 4, 16),
    0x1C: ("BC3", 4, 4, 16),
    0x1D: ("BC4U", 4, 4, 8),
    0x1E: ("BC5U", 4, 4, 16),
    0x1F: ("BC6H_UF16", 4, 4, 16),
    0x20: ("BC7", 4, 4, 16),
}


def load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_texture(data: bytes, table: int) -> tuple[str, int, int, int, int, int, bytes]:
    if data[table : table + 4] != b"BRTI":
        raise RuntimeError(f"Invalid BRTI offset 0x{table:X}")
    texture_format = struct.unpack_from("<I", data, table + 0x1C)[0]
    width = struct.unpack_from("<I", data, table + 0x24)[0]
    height = struct.unpack_from("<I", data, table + 0x28)[0]
    layout = struct.unpack_from("<I", data, table + 0x34)[0]
    image_size = struct.unpack_from("<I", data, table + 0x50)[0]
    alignment = struct.unpack_from("<I", data, table + 0x54)[0]
    name_pointer = struct.unpack_from("<Q", data, table + 0x60)[0]
    mip_table = struct.unpack_from("<Q", data, table + 0x70)[0]
    image_pointer = struct.unpack_from("<Q", data, mip_table)[0]
    name_length = struct.unpack_from("<H", data, name_pointer)[0]
    name = data[name_pointer + 2 : name_pointer + 2 + name_length].rstrip(b"\0").decode("utf-8")
    image = data[image_pointer : image_pointer + image_size]
    if len(image) != image_size:
        raise RuntimeError(f"Truncated image data for {name}")
    return name, width, height, texture_format, layout, alignment, image


def decode_file(
    source: Path, output: Path, swizzle, dds, requested: set[str] | None = None
) -> list[Path]:
    data = source.read_bytes()
    if data[:4] != b"BNTX":
        raise RuntimeError(f"Not a BNTX file: {source}")
    texture_count = struct.unpack_from("<I", data, 0x24)[0]
    pointer_array = struct.unpack_from("<Q", data, 0x28)[0]
    written = []
    for index in range(texture_count):
        table = struct.unpack_from("<Q", data, pointer_array + index * 8)[0]
        name, width, height, texture_format, layout, alignment, image = read_texture(data, table)
        if requested is not None and name not in requested:
            continue
        format_code = texture_format >> 8
        if format_code not in FORMATS:
            raise RuntimeError(f"Unsupported format 0x{texture_format:X} for {name}")
        dds_format, block_width, block_height, bytes_per_block = FORMATS[format_code]
        linear = swizzle.deswizzle(
            width,
            height,
            block_width,
            block_height,
            bytes_per_block,
            1,
            alignment,
            layout & 7,
            image,
        )
        linear_size = ((width + block_width - 1) // block_width) * (
            (height + block_height - 1) // block_height
        ) * bytes_per_block
        header = dds.generateHeader(1, width, height, dds_format, [5, 4, 3, 2], linear_size, True)
        destination = output / f"{name}.png"
        destination.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(io.BytesIO(header + linear[:linear_size])) as decoded:
            decoded.convert("RGBA").save(destination, optimize=True)
        written.append(destination)
    return written


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--extractor", type=Path, required=True)
    parser.add_argument("--texture", action="append")
    args = parser.parse_args()
    swizzle = load_module(args.extractor / "swizzle.py", "plza_swizzle")
    dds = load_module(args.extractor / "dds.py", "plza_dds")
    sources = sorted(args.input.rglob("*.bntx")) if args.input.is_dir() else [args.input]
    count = 0
    for source in sources:
        requested = set(args.texture) if args.texture else None
        written = decode_file(source, args.output, swizzle, dds, requested)
        count += len(written)
        for destination in written:
            print(f"Decoded {destination.name}")
    print(f"Decoded {count} textures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
