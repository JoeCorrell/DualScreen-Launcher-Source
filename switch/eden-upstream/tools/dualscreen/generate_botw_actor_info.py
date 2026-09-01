#!/usr/bin/env python3



from __future__ import annotations

import argparse
from pathlib import Path

import oead


def integer(actor: oead.byml.Hash, name: str) -> int:
    return int(actor[name]) if name in actor else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("actor_info", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = args.actor_info.read_bytes()
    if data[:4] == b"Yaz0":
        data = oead.yaz0.decompress(data)
    document = oead.byml.from_binary(data)

    rows: list[tuple[str, int, int, int]] = []
    for actor in document["Actors"]:
        name = str(actor["name"])
        attack = integer(actor, "attackPower")
        guard = integer(actor, "weaponCommonGuardPower")
        defense = integer(actor, "armorDefenceAddLevel")
        if attack or guard or defense or name.startswith(("Weapon_", "Armor_")):
            rows.append((name, attack, guard, defense))
    rows.sort(key=lambda row: row[0])

    lines = [
        "// Generated from the user's unmodified BOTW ActorInfo.product.sbyml.",
        "// Do not hand-edit; regenerate with tools/dualscreen/generate_botw_actor_info.py.",
        "constexpr std::array<ActorStats, %d> BotwActorStats{{" % len(rows),
    ]
    for name, attack, guard, defense in rows:
        lines.append(f'    ActorStats{{"{name}", {attack}, {guard}, {defense}}},')
    lines.append("}};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {len(rows)} BOTW actor-stat rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
