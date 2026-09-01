#!/usr/bin/env python3









from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("item_text", type=Path)
    parser.add_argument("actor_info", type=Path)
    parser.add_argument("stock_directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    text_source = args.item_text.read_text(encoding="utf-8")
    actor_source = args.actor_info.read_text(encoding="utf-8")
    names = {
        match.group(1): bytes(match.group(2), "utf-8").decode("unicode_escape")
        for match in re.finditer(r'ItemText\{"([^"]+)", "((?:\\.|[^"])*)",', text_source)
    }
    actors = set(re.findall(r'ActorStats\{"([^"]+)"', actor_source))
    packaged = {path.stem for path in args.stock_directory.glob("*.png")}

    icon_by_name_and_slot: dict[tuple[str, str], list[str]] = defaultdict(list)
    for icon in packaged:
        actor = icon
        match = re.fullmatch(r"Obj_Head_(\d{3})", icon)
        if match:
            actor = f"Armor_{match.group(1)}_Head"
        name = names.get(actor)
        if name:
            icon_by_name_and_slot[(name, actor.rsplit("_", 1)[-1])].append(icon)

    aliases: dict[str, str] = {}
    for actor in sorted(actors):
        if actor in packaged or actor not in names:
            continue
        key = (names[actor], actor.rsplit("_", 1)[-1])
        candidates = sorted(icon_by_name_and_slot[key])
        if candidates:
            aliases[actor] = candidates[0]



    for actor in sorted(actors):
        if not actor.endswith("_B") or actor in aliases or actor in packaged:
            continue
        base_actor = actor[:-2]
        target = aliases.get(base_actor, base_actor)
        if target in packaged:
            aliases[actor] = target

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(aliases, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Generated {len(aliases)} StockItem icon aliases in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
