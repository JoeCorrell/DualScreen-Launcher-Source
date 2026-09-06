#!/usr/bin/env python3








from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import oead


SUPPORTED_PREFIXES = (
    "Weapon_Sword_",
    "Weapon_Lsword_",
    "Weapon_Spear_",
    "Weapon_Bow_",
    "Weapon_Shield_",
)


def read_pack(path: Path) -> oead.Sarc:
    data = path.read_bytes()
    if data[:4] == b"Yaz0":
        data = oead.yaz0.decompress(data)
    return oead.Sarc(data)


def read_aamp(pack: oead.Sarc, component: str) -> oead.aamp.ParameterIO:
    matches = [file for file in pack.get_files() if f"Actor/{component}/" in file.name]
    if len(matches) != 1:
        raise ValueError(f"expected one {component} entry, found {len(matches)}")
    return oead.aamp.ParameterIO.from_binary(bytes(matches[0].data))


def string_parameter(value: object) -> str:
    return str(value)


def vector_parameter(value: object) -> list[float]:
    return [round(float(value.x), 6), round(float(value.y), 6), round(float(value.z), 6)]


def attachment_bone(actor: str) -> str:
    if actor.startswith(("Weapon_Bow_", "Weapon_Shield_")):
        return "Armature_Pod_A"
    return "Armature_Weapon_R"


def actor_entry(path: Path) -> tuple[str, dict[str, object], tuple[str, str]]:
    actor = path.name.removesuffix(".sbactorpack")
    pack = read_pack(path)

    model_list = read_aamp(pack, "ModelList")
    model_data = model_list.lists["ModelData"].lists["ModelData_0"]
    folder = string_parameter(model_data.objects["Base"].params["Folder"].v)


    unit = folder
    if "Unit" in model_data.lists:
        unit_list = model_data.lists["Unit"]
    else:
        unit_list = None
    if unit_list is not None and "Unit_0" in unit_list.objects:
        unit = string_parameter(unit_list.objects["Unit_0"].params["UnitName"].v)
    if not folder or not unit:
        raise ValueError("empty model folder or unit name")

    entry: dict[str, object] = {"file": f"{unit}.glb"}
    if not actor.startswith(("Armor_", "Item_", "Animal_Insect_")):
        general = read_aamp(pack, "GeneralParamList")
        holders = []
        for name, parameters in general.objects.items():
            if (
                "PlayerHoldTransOffset" in parameters.params
                and "PlayerHoldRotOffset" in parameters.params
            ):
                holders.append((str(name), parameters))
        if len(holders) != 1:
            raise ValueError(f"expected one PlayerHold transform, found {len(holders)}")
        _, holder = holders[0]
        entry["attachment"] = {
            "bone": attachment_bone(actor),
            "translation": vector_parameter(holder.params["PlayerHoldTransOffset"].v),
            "rotationDegrees": vector_parameter(holder.params["PlayerHoldRotOffset"].v),
        }
    return actor, entry, (folder, unit)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--actor-pack-root", type=Path, required=True)
    parser.add_argument("--include-armor", action="store_true")
    parser.add_argument("--materials-only", action="store_true",
                        help="Export ingredient model mappings; attachment comes from live CarryBox, not equipment offsets")
    parser.add_argument("--actor-info", type=Path,
                        help="ActorInfo.product.sbyml supplies resident ingredient models absent from standalone actor packs")
    parser.add_argument(
        "--item-text-table",
        type=Path,
        help="Optional generated botw_item_text.inc used to exclude non-pouch helper actors",
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    pack_directory = args.actor_pack_root / "Actor" / "Pack"
    pouch_actors: set[str] | None = None
    if args.item_text_table is not None:
        table = args.item_text_table.read_text(encoding="utf-8")
        pouch_actors = set(re.findall(r'ItemText\{"([^"]+)"', table))
    actors: dict[str, dict[str, object]] = {}
    models: dict[str, dict[str, object]] = {}
    errors: dict[str, str] = {}
    prefixes = SUPPORTED_PREFIXES + (("Armor_",) if args.include_armor else ())
    if args.materials_only:
        prefixes = ("Item_", "Animal_Insect_")
    for path in sorted(pack_directory.glob("*.sbactorpack")):
        actor = path.name.removesuffix(".sbactorpack")
        if not actor.startswith(prefixes):
            continue
        if args.materials_only and actor.startswith(("Item_Cook_", "Item_Roast_", "Item_Chilled_")):
            continue
        if pouch_actors is not None and actor not in pouch_actors:
            continue
        try:
            actor, entry, (folder, unit) = actor_entry(path)
            actors[actor] = entry
            model = models.setdefault(
                unit,
                {
                    "folder": folder,
                    "modelPath": f"/Model/{folder}.sbfres",
                    "texturePath": f"/Model/{folder}.Tex.sbfres",
                    "actors": [],
                },
            )
            if model["folder"] != folder:
                raise ValueError(f"unit {unit} is supplied by more than one model folder")
            model["actors"].append(actor)
        except Exception as exception:
            errors[actor] = str(exception)

    if args.materials_only and args.actor_info:
        data = args.actor_info.read_bytes()
        if data[:4] == b'Yaz0':
            data = oead.yaz0.decompress(data)
        for info in oead.byml.from_binary(data)['Actors']:
            actor = str(info['name'])
            if (actor in actors or not actor.startswith(prefixes) or
                    actor.startswith(('Item_Cook_', 'Item_Roast', 'Item_Chilled')) or
                    (pouch_actors is not None and actor not in pouch_actors)):
                continue
            folder = str(info['bfres']) if 'bfres' in info else ''
            unit = str(info['mainModel']) if 'mainModel' in info else ''
            if not folder or not unit:
                continue
            model = models.setdefault(unit, {'folder': folder,
                'modelPath': f'/Model/{folder}.sbfres',
                'texturePath': f'/Model/{folder}.Tex.sbfres', 'actors': []})
            if model['folder'] != folder:
                raise ValueError(f'Conflicting resident model folder: {unit}')
            model['actors'].append(actor)
            actors[actor] = {'file': f'{unit}.glb'}
            errors.pop(actor, None)

    for model in models.values():
        model["actors"].sort()
    result = {
        "actors": dict(sorted(actors.items())),
        "models": dict(sorted(models.items())),
        "errors": dict(sorted(errors.items())),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        f"Generated {len(actors)} actor mappings for {len(models)} unique models; "
        f"{len(errors)} errors"
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
