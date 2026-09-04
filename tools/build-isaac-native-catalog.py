#!/usr/bin/env python3
"""Generate the compact catalog consumed by the in-process Isaac panel.

The source JSON is generated from the exact v1.9.7.17.J460 XML and ANM2
resources.  The native panel intentionally uses a line-oriented format so its
game-process parser stays bounded and dependency-free.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from urllib.parse import quote


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INDEX = ROOT / "pc" / "mods" / "IsaacSecondScreen" / "assets" / "index.json"
DEFAULT_OUTPUT = ROOT / "pc" / "mods" / "IsaacSecondScreen" / "assets" / "catalog.tsv"
ANIMATION_ACTORS = {
    "hudpickups",
    "minimap_icons",
    "ui_cardfronts",
    "ui_cardspills",
    "ui_hearts",
}


def encoded(value: object) -> str:
    return quote(str(value or ""), safe="-._~ /:#+'()[]")


def asset(value: object) -> str:
    path = str(value or "").replace("\\", "/")
    return encoded(path.removeprefix("isaac/"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--index", type=Path, default=DEFAULT_INDEX)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    root = json.loads(args.index.read_text(encoding="utf-8"))
    lines = [f"V\t1\t{encoded(root['sourceBuild'])}"]

    for marker, category in (("C", "collectibles"), ("T", "trinkets")):
        entries = root["items"][category]
        for key in sorted(entries, key=lambda value: int(value)):
            entry = entries[key]
            lines.append(
                "\t".join(
                    (
                        marker,
                        str(entry["id"]),
                        str(entry.get("quality", -1)),
                        str(entry.get("maxCharges", 0)),
                        asset(entry.get("asset")),
                        encoded(entry.get("name")),
                        encoded(entry.get("description")),
                    )
                )
            )

    for key in sorted(root["players"], key=lambda value: int(value)):
        entry = root["players"][key]
        lines.append(
            "\t".join(
                (
                    "P",
                    str(entry["id"]),
                    asset(entry.get("asset")),
                    encoded(entry.get("name")),
                )
            )
        )

    for key in sorted(root["pockets"], key=lambda value: int(value)):
        entry = root["pockets"][key]
        lines.append(
            "\t".join(
                (
                    "K",
                    str(entry["id"]),
                    str(entry.get("pickupFrame", 0)),
                    encoded(entry.get("name")),
                    encoded(entry.get("description")),
                )
            )
        )

    for actor in sorted(ANIMATION_ACTORS):
        animations = root["animations"][actor]["animations"]
        for name in sorted(animations):
            frames = animations[name]["frames"]
            for frame_index, frame in enumerate(frames):
                # Some ANM2 effects reference sheets that are not part of the
                # HUD asset set. They are not drawable and are never used by
                # this panel, so omit them instead of emitting an empty path.
                if not frame.get("asset"):
                    continue
                lines.append(
                    "\t".join(
                        (
                            "A",
                            encoded(actor),
                            encoded(name),
                            str(frame_index),
                            asset(frame.get("asset")),
                            str(frame["x"]),
                            str(frame["y"]),
                            str(frame["w"]),
                            str(frame["h"]),
                        )
                    )
                )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(lines) - 1} catalog records to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
