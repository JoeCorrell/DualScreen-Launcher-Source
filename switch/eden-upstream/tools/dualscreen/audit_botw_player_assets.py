#!/usr/bin/env python3



from __future__ import annotations

import argparse
import json
import re
import struct
from pathlib import Path


EQUIPMENT_PREFIXES = (
    "Weapon_Sword_",
    "Weapon_Lsword_",
    "Weapon_Spear_",
    "Weapon_Bow_",
    "Weapon_Shield_",
)


def expected_bone(actor: str) -> str:
    if actor.startswith(("Weapon_Bow_", "Weapon_Shield_")):
        return "Armature_Pod_A"
    return "Armature_Weapon_R"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--index", type=Path, required=True)
    parser.add_argument("--item-text-table", type=Path, required=True)
    args = parser.parse_args()

    root = args.index.parent
    index = json.loads(args.index.read_text(encoding="utf-8"))
    table = args.item_text_table.read_text(encoding="utf-8")
    expected = {
        actor
        for actor in re.findall(r'ItemText\{"([^"]+)', table)
        if actor.startswith(EQUIPMENT_PREFIXES)
    }
    actual = {
        actor for actor in index["actors"] if actor.startswith(EQUIPMENT_PREFIXES)
    }
    failures: list[str] = []
    if expected != actual:
        failures.append(f"missing={sorted(expected - actual)}, extra={sorted(actual - expected)}")

    referenced_files: set[str] = set()
    for actor, value in index["actors"].items():
        if isinstance(value, str):
            file_name = value
        else:
            file_name = value["file"]
            if actor.startswith(EQUIPMENT_PREFIXES):
                attachment = value.get("attachment", {})
                if attachment.get("bone") != expected_bone(actor):
                    failures.append(f"{actor} has invalid attachment bone")
                for key in ("translation", "rotationDegrees"):
                    vector = attachment.get(key)
                    if not isinstance(vector, list) or len(vector) != 3:
                        failures.append(f"{actor} has invalid {key}")
        referenced_files.add(file_name)
        path = root / file_name
        if not path.is_file():
            failures.append(f"{actor} references missing {file_name}")
            continue
        data = path.read_bytes()[:12]
        if len(data) != 12:
            failures.append(f"{file_name} is shorter than a GLB header")
            continue
        magic, version, total_length = struct.unpack("<4sII", data)
        if magic != b"glTF" or version != 2 or total_length != path.stat().st_size:
            failures.append(f"{file_name} has an invalid GLB header")

    result = {
        "actorMappings": len(index["actors"]),
        "equipmentMappings": len(actual),
        "referencedGlbs": len(referenced_files),
        "failures": failures,
    }
    print(json.dumps(result, separators=(",", ":")))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
