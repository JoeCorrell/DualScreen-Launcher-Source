#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image


def symbol_mask(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGBA")
    output = Image.new("RGBA", image.size)
    output.putdata([(255, 255, 255, min(red, green)) for red, green, _, _ in image.getdata()])
    destination.parent.mkdir(parents=True, exist_ok=True)
    output.save(destination, optimize=True)


def player_icon(source: Path, destination: Path) -> None:
    image = Image.open(source).convert("RGBA")
    output = Image.new("RGBA", image.size)
    output.putdata([
        (255, 232, 57, green) if red > 128 else (15, 91, 86, green)
        for red, green, _, _ in image.getdata()
    ])
    destination.parent.mkdir(parents=True, exist_ok=True)
    output.save(destination, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    sources = {
        "center": "map_icon_center_color_00^t.png",
        "cafe": "map_icon_cafe_color_00^t.png",
        "shop": "map_icon_boutique_color_00^t.png",
    }
    for name, source in sources.items():
        symbol_mask(args.input / source, args.output / f"{name}.png")
    player_icon(args.input / "map_icon_player_00^t.png", args.output / "player.png")
    print(f"Prepared {len(sources) + 1} native map icons")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
