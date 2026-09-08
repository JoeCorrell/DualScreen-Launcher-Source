#!/usr/bin/env python3
"""Build the Android-side index for extracted Binding of Isaac resources.

This deliberately derives identifiers and sprite rectangles from the installed
game's XML/ANM2 resources.  It avoids a second, hand-maintained table drifting
away from the exact executable/resources pair supported by the launcher.
"""

from __future__ import annotations

import argparse
import json
import re
import xml.etree.ElementTree as ET
from pathlib import Path, PurePosixPath


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--resources", type=Path, required=True)
    parser.add_argument("--metadata-resources", type=Path)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def humanize(token: str | None) -> str:
    if not token:
        return "Unknown"
    value = token.removeprefix("#")
    value = re.sub(r"_(NAME|DESCRIPTION)$", "", value)
    return value.replace("_", " ").strip().title()


def asset_lookup(assets: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in assets.rglob("*"):
        if path.is_file():
            relative = path.relative_to(assets).as_posix()
            result[relative.casefold()] = f"isaac/{relative}"
    return result


def resolve_asset(paths: dict[str, str], relative: str | None) -> str | None:
    if not relative:
        return None
    normalized = PurePosixPath(relative.replace("\\", "/")).as_posix().lstrip("/")
    candidates = [normalized]
    if normalized.casefold().startswith("resources/"):
        candidates.append(normalized[len("resources/"):])
    if normalized.casefold().startswith("gfx/"):
        candidates.append(f"game/{normalized}")
    else:
        candidates.append(f"game/gfx/items/{normalized}")
        candidates.append(f"game/gfx/items/collectibles/{normalized}")
        candidates.append(f"game/gfx/items/trinkets/{normalized}")
    for candidate in candidates:
        found = paths.get(candidate.casefold())
        if found:
            return found
    return None


def frames_from_anm2(path: Path, paths: dict[str, str]) -> dict:
    root = ET.parse(path).getroot()
    sheets: dict[str, str | None] = {}
    for sheet in root.findall("./Content/Spritesheets/Spritesheet"):
        raw = sheet.get("Path")
        relative = f"game/gfx/ui/{raw}" if raw else None
        sheets[sheet.get("Id", "0")] = resolve_asset(paths, relative)
    layers = {
        layer.get("Id", "0"): layer.get("SpritesheetId", "0")
        for layer in root.findall("./Content/Layers/Layer")
    }
    animations: dict[str, dict] = {}
    for animation in root.findall("./Animations/Animation"):
        collected: list[dict] = []
        for layer_animation in animation.findall("./LayerAnimations/LayerAnimation"):
            layer_id = layer_animation.get("LayerId", "0")
            sheet = sheets.get(layers.get(layer_id, "0"))
            for frame in layer_animation.findall("./Frame"):
                if frame.get("Visible", "true").casefold() == "false":
                    continue
                collected.append(
                    {
                        "asset": sheet,
                        "x": int(frame.get("XCrop", "0")),
                        "y": int(frame.get("YCrop", "0")),
                        "w": int(frame.get("Width", "0")),
                        "h": int(frame.get("Height", "0")),
                        "pivotX": int(frame.get("XPivot", "0")),
                        "pivotY": int(frame.get("YPivot", "0")),
                        "delay": int(frame.get("Delay", "1")),
                    }
                )
        if collected:
            animations[animation.get("Name", "Unnamed")] = {
                "frames": collected,
                "loop": animation.get("Loop", "false").casefold() == "true",
            }
    return {
        "default": root.find("./Animations").get("DefaultAnimation", "") if root.find("./Animations") is not None else "",
        "animations": animations,
    }


def items(resources: Path, paths: dict[str, str]) -> dict[str, dict[str, dict]]:
    metadata: dict[str, dict] = {}
    metadata_path = resources / "items_metadata.xml"
    if metadata_path.exists():
        for element in ET.parse(metadata_path).getroot():
            if element.tag != "item":
                continue
            item_id = element.get("id")
            if item_id:
                metadata[item_id] = {
                    "quality": int(element.get("quality", "-1")),
                    "tags": element.get("tags", "").split(),
                }

    result: dict[str, dict[str, dict]] = {"collectibles": {}, "trinkets": {}}
    for element in ET.parse(resources / "items.xml").getroot():
        item_id = element.get("id")
        if not item_id or not item_id.isdigit():
            continue
        category = "trinkets" if element.tag == "trinket" else "collectibles"
        # The tail of items.xml also contains internal costume/effect records
        # which reuse collectible IDs and intentionally have no inventory art.
        if category == "collectibles" and element.tag not in {"active", "passive", "familiar"}:
            continue
        raw_name = element.get("name")
        raw_description = element.get("description")
        entry = {
            "id": int(item_id),
            "kind": element.tag,
            "name": humanize(raw_name),
            "nameKey": raw_name,
            "description": humanize(raw_description),
            "descriptionKey": raw_description,
            "asset": resolve_asset(paths, element.get("gfx")),
            "maxCharges": int(element.get("maxcharges", "0")),
            "cache": element.get("cache", "").split(),
        }
        if category == "collectibles":
            entry.update(metadata.get(item_id, {"quality": -1, "tags": []}))
        else:
            entry.update({"quality": -1, "tags": []})
        result[category][item_id] = entry
    return result


def players(resources: Path, paths: dict[str, str]) -> dict[str, dict]:
    result: dict[str, dict] = {}
    for player in ET.parse(resources / "players.xml").getroot().findall("player"):
        player_id = player.get("id")
        if not player_id or not player_id.isdigit():
            continue
        portrait = player.get("portrait")
        result[player_id] = {
            "id": int(player_id),
            "name": humanize(player.get("name")),
            "nameKey": player.get("name"),
            "asset": resolve_asset(paths, f"game/gfx/ui/stage/{portrait}" if portrait else None),
        }
    return result


def pockets(resources: Path) -> dict[str, dict]:
    result: dict[str, dict] = {}
    for pocket in ET.parse(resources / "pocketitems.xml").getroot():
        pocket_id = pocket.get("id")
        if not pocket_id or not pocket_id.isdigit():
            continue
        result[pocket_id] = {
            "id": int(pocket_id),
            "kind": pocket.tag,
            "type": pocket.get("type", pocket.tag),
            "pickupFrame": int(pocket.get("pickup", "0")),
            "name": humanize(pocket.get("name")),
            "nameKey": pocket.get("name"),
            "description": humanize(pocket.get("description")),
            "descriptionKey": pocket.get("description"),
        }
    return result


def main() -> None:
    args = parse_args()
    resources = args.resources.resolve()
    assets = args.assets.resolve()
    paths = asset_lookup(assets)
    ui = resources / "gfx" / "ui"
    animation_files = [
        "hudpickups.anm2",
        "minimap_icons.anm2",
        "minimap1.anm2",
        "minimap2.anm2",
        "ui_hearts.anm2",
        "ui_cardfronts.anm2",
        "ui_cardspills.anm2",
        "ui_chargebar.anm2",
    ]
    output = {
        "schema": 1,
        "sourceBuild": "Binding of Isaac: Repentance+ v1.9.7.17.J460",
        "items": items(resources, paths),
        "players": players(resources, paths),
        "pockets": pockets(resources),
        "animations": {
            name.removesuffix(".anm2"): frames_from_anm2(ui / name, paths)
            for name in animation_files
        },
    }
    metadata_resources = args.metadata_resources or resources
    strings = {}
    for filename in ("stringtable.sta", "stringtable_pc.sta"):
        path = metadata_resources / filename
        if path.exists():
            for key in ET.parse(path).getroot().iter("key"):
                strings[key.get("name")] = key.findtext("string", "")
    for collection in (*output["items"].values(), output["players"], output["pockets"]):
        for entry in collection.values():
            for field in ("name", "description"):
                token = entry.get(field + "Key")
                if token:
                    entry[field] = strings.get(token.removeprefix("#"), token if not token.startswith("#") else entry.get(field, ""))
    progress = {"achievements": [], "challenges": [], "bosses": []}
    for category, filename, tag in (("achievements", "achievements.xml", "achievement"), ("challenges", "challenges.xml", "challenge"), ("bosses", "bossportraits.xml", "boss")):
        path = metadata_resources / filename
        if not path.exists(): continue
        tree = ET.parse(path, parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
        comment = ""
        for element in tree.getroot():
            if element.tag is ET.Comment:
                comment = " ".join((element.text or "").split())
                continue
            if element.tag != tag: continue
            entry = dict(element.attrib)
            if not entry.get("id", "").isdigit(): continue
            entry["id"] = int(entry["id"])
            if category == "achievements":
                entry["requirement"] = comment or entry.get("steam_description", "")
            if category == "bosses":
                entry["asset"] = resolve_asset(paths, "game/gfx/ui/boss/" + entry.get("portrait", ""))
            comment = ""
            progress[category].append(entry)
    output["progress"] = progress
    all_items = [
        entry
        for category in output["items"].values()
        for entry in category.values()
    ]
    missing = [entry["id"] for entry in all_items if not entry["asset"]]
    if missing:
        raise SystemExit(f"missing Android assets for {len(missing)} items; first: {missing[:12]}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, separators=(",", ":"), ensure_ascii=False), encoding="utf-8")
    print(
        f"wrote {args.output}: {len(output['items']['collectibles'])} collectibles, "
        f"{len(output['items']['trinkets'])} trinkets, "
        f"{len(output['players'])} players, {len(output['pockets'])} pocket items"
    )


if __name__ == "__main__":
    main()
