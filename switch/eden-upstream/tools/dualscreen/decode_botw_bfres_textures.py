#!/usr/bin/env python3



from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import struct
import sys
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--bntx-extractor", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--texture",
        action="append",
        default=[],
        help="Decode only this exact BNTX texture name (repeatable)",
    )
    args = parser.parse_args()

    sys.path.insert(0, str(args.bntx_extractor))
    import bntx_extract

    args.output.mkdir(parents=True, exist_ok=True)
    sources: list[Path] = []
    for source in args.inputs:
        if source.is_dir():
            sources.extend(sorted(source.rglob("*.sbitemico")))
        else:
            sources.append(source)
    sources = [source.resolve() for source in sources]
    args.output = args.output.resolve()
    original_directory = Path.cwd()
    os.chdir(args.output)
    try:
        for source in sources:
            compressed = source.read_bytes()
            if compressed[:4] == b"Yaz0":
                import oead

                bfres = oead.yaz0.decompress(compressed)
            else:
                bfres = compressed
            bntx_offset = bfres.find(b"BNTX")
            if bntx_offset < 0:
                raise RuntimeError(f"No embedded BNTX found in {source}")
            if bntx_offset == 0:
                bntx = bfres
            else:
                bntx_size = struct.unpack_from("<I", bfres, bntx_offset + 0x1C)[0]
                bntx = bfres[bntx_offset : bntx_offset + bntx_size]
            with contextlib.redirect_stdout(io.StringIO()):
                textures = bntx_extract.readBNTX(bntx)
            selected = [texture for texture in textures if not args.texture or texture.name in args.texture]
            if args.texture and not selected:
                raise RuntimeError(f"None of the requested textures exist in {source}")
            with contextlib.redirect_stdout(io.StringIO()):
                bntx_extract.saveTextures(selected)
            for texture in selected:
                dds_path = args.output / f"{texture.name}.dds"
                astc_path = args.output / f"{texture.name}.astc"
                if astc_path.exists():
                    raise RuntimeError(f"ASTC conversion is not configured for {texture.name}")
                if not dds_path.exists():
                    raise RuntimeError(f"BNTX extractor did not produce {texture.name}.dds")
                image = Image.open(dds_path)
                if args.texture:
                    safe_name = re.sub(r"[^A-Za-z0-9_.-]+", "_", texture.name).strip("_")
                    destination = args.output / f"{safe_name}.png"
                else:
                    destination = args.output / f"{source.stem}.png"
                image.save(destination)
                dds_path.unlink()
                print(f"Decoded {source.name} -> {destination.name} ({image.width}x{image.height})")
    finally:
        os.chdir(original_directory)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
