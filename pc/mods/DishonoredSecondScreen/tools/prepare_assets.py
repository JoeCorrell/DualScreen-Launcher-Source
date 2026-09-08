"""Prepare original, locally extracted UI resources; never modifies game files.

Input is UModel's UI_Journal_SF Texture2D export and FFDec's DisFonts_SF font
export. The output is a local payload staging directory, not an APK asset tree.
No generated artwork or sample game-state values are introduced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import re
from PIL import Image

IMAGES = {
    "journal-background": "Journal_I44",
    # Three whole-frame variants and their matching transparent glow frames.
    # These exports are not left/middle/right slices or parchment textures.
    "frame-idle-a": "Journal_I1A",
    "frame-idle-b": "Journal_I1C",
    "frame-idle-c": "Journal_I1E",
    "frame-selected-a": "Journal_I21",
    "frame-selected-b": "Journal_I23",
    "frame-selected-c": "Journal_I25",
    "occult-ring": "Journal_I2A",
    "power-blink": "Journal_I86",
    "power-dark-vision": "Journal_I80",
    "power-possession": "Journal_I8A",
    "power-bend-time": "Journal_I88",
    "power-windblast": "Journal_I82",
    "power-devouring-swarm": "Journal_I84",
    "power-vitality": "Journal_I7E",
    "power-agility": "Journal_I7C",
    "power-blood-thirsty": "Journal_I7A",
    "power-shadow-kill": "Journal_I78",
    "coin": "Journal_IC8",
}
FONTS = {
    "body.ttf": "1_$NormalFont_ChaletComprime-CologneEighty.ttf",
    "title.ttf": "2_$TitleFont_Emerge BF.ttf",
}
MAPS = {
    "map-boyle": (1, "UI_Map_Boyle_SF", "LevelMap_Ext_Boyle"),
    "map-bridge": (2, "UI_Map_Bridge_SF", "LevelMap_Ext_Bridge"),
    "map-brothel-exterior": (3, "UI_Map_BrothelExt_SF", "LevelMap_Ext_Brothel"),
    "map-brothel-interior": (4, "UI_Map_BrothelInt_SF", "LevelMap_Int_Brothel"),
    "map-lighthouse-1": (5, "UI_Map_LightHouseExt1_SF", "LevelMap_Ext_LightHouse_1"),
    "map-overseer-exterior": (6, "UI_Map_OverseerExt_SF", "LevelMap_Ext_Overseer"),
    "map-overseer-interior-1": (7, "UI_Map_OverseerInt1_SF", "LevelMap_Int_Overseer_1"),
    "map-overseer-interior-2": (8, "UI_Map_OverseerInt2_SF", "LevelMap_Int_Overseer_2"),
    "map-streets": (9, "UI_Map_Streets_SF", "LevelMap_Ext_Streets"),
    "map-tower-interior-1": (10, "UI_Map_TowerReturnInt_SF", "LevelMap_Int_TowerReturn"),
    "map-lighthouse-2": (11, "UI_Map_LightHouseExt2_SF", "LevelMap_Ext_LightHouse_2"),
    "map-lighthouse-3": (12, "UI_Map_LightHouseExt3_SF", "LevelMap_Ext_LightHouse_3"),
    "map-tower-interior-2": (13, "UI_Map_TowerReturnInt2_SF", "LevelMap_Int_TowerReturn_02"),
}

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def original_inputs(texture_dir, font_dir, map_dir, item_dir, portrait_dir):
    inputs = [(key + ".png", texture_dir / (name + ".tga"), "UI_Journal_SF")
              for key, name in IMAGES.items()]
    inputs += [(key, font_dir / name, "DisFonts_SF") for key, name in FONTS.items()]
    inputs += [(key + ".png", map_dir / package / "Texture2D" / (export + ".tga"), package)
               for key, (_, package, export) in MAPS.items()]
    catalog = Path(__file__).resolve().parents[1] / "src" / "ItemAssets.inc"
    item_names = []
    for line in catalog.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("//"):
            continue
        match = re.fullmatch(r"DIS_ITEM_ASSET\(([A-Za-z0-9_]+)\)", line)
        if not match or match[1] in item_names:
            raise ValueError("Invalid or duplicate original item catalog entry")
        item_names.append(match[1])
    inputs += [("items/" + name + "_Large.png", item_dir / (name + "_Large.tga"), "UI_ItemIcons_Large")
               for name in item_names]
    portrait_names = []
    portrait_catalog = Path(__file__).resolve().parents[1] / "src" / "PortraitAssets.inc"
    for line in portrait_catalog.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("//"):
            continue
        match = re.fullmatch(r"DIS_PORTRAIT_ASSET\(([A-Za-z0-9_]+)\)", line)
        if not match or match[1] in portrait_names:
            raise ValueError("Invalid or duplicate original portrait catalog entry")
        portrait_names.append(match[1])
    inputs += [("portraits/" + name + ".png", portrait_dir / (name + ".tga"), "UI_portraits_objectives")
               for name in portrait_names]
    missing = [str(source) for _, source, _ in inputs if not source.is_file()]
    if missing:
        raise ValueError("Missing original exports:\n" + "\n".join(missing))
    return inputs, item_names, portrait_names


def inspect_inputs(inputs):
    """Decode original exports read-only, before creating any output directory."""
    pixels = 0
    names = set()
    for name, source, _ in inputs:
        if name in names:
            raise ValueError(f"Duplicate resource destination: {name}")
        names.add(name)
        if source.suffix == ".tga":
            with Image.open(source) as original:
                original.load()
                width, height = original.size
                if not (0 < width <= 4096 and 0 < height <= 4096):
                    raise ValueError(f"Invalid original resource dimensions: {source}")
                if name.startswith("frame-") and original.size != (184, 148):
                    raise ValueError(f"Native frame geometry changed: {source}")
                pixels += width * height
        else:
            # Both allowlisted originals are TrueType sfnt files; reject an
            # HTML download/error page before the runtime attempts font loading.
            data = source.read_bytes()
            if len(data) < 12 or data[:4] != b"\x00\x01\x00\x00":
                raise ValueError(f"Original TrueType export is invalid: {source}")
    if pixels > 32 * 1024 * 1024:
        raise ValueError("Original images exceed the runtime's decoded resource budget")
    return {"resources": len(inputs), "imagePixels": pixels, "imageBudget": 32 * 1024 * 1024}


def prepare(texture_dir, font_dir, map_dir, item_dir, portrait_dir, output):
    inputs, item_names, portrait_names = original_inputs(texture_dir, font_dir, map_dir, item_dir, portrait_dir)
    inspect_inputs(inputs)
    # Validate every destination before the first mutation, including sources
    # whose error would otherwise be noticed after earlier files were written.
    for name, source, _ in inputs:
        if source.resolve() == (output / name).resolve():
            raise ValueError("Output must not overwrite an input")
    output.mkdir(parents=True, exist_ok=True)
    manifest = {"schema": 1, "source": "user-installed Dishonored cooked packages",
                "note": "Asset role names are presentation mappings, not ownership or power unlock predicates.",
                "files": []}
    for name, source, package in inputs:
        target = output / name
        if source.resolve() == target.resolve():
            raise ValueError("Output must not overwrite an input")
        target.parent.mkdir(parents=True, exist_ok=True)
        row = {"file": name, "package": package, "export": source.name,
               "sourceSha256": sha256(source)}
        if Path(name).stem in MAPS:
            native, package_name, export = MAPS[Path(name).stem]
            row.update({"nativeMapId": native, "nativeObject": package_name + "." + export})
        if name.startswith("items/"):
            native = Path(name).stem.removesuffix("_Large")
            row.update({"assetId": 1000 + item_names.index(native),
                        "nativeObject": "UI_ItemIcons_Large." + Path(name).stem})
        if name.startswith("portraits/"):
            row.update({"assetId": 3000 + portrait_names.index(Path(name).stem),
                        "nativeObject": "UI_portraits_objectives." + Path(name).stem})
        if source.suffix == ".tga":
            # Lossless format conversion only; retain original pixels/alpha.
            with Image.open(source) as original:
                original.convert("RGBA").save(target)
                row["width"], row["height"] = original.size
        else:
            shutil.copyfile(source, target)
        row["sha256"] = sha256(target)
        manifest["files"].append(row)
    (output / "assets.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(inputs)} original resources in {output}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("textures", type=Path)
    parser.add_argument("fonts", type=Path)
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument("--check", action="store_true", help="Read-only input inspection; no conversion, staging, or packaging")
    parser.add_argument("--maps", type=Path, required=True, help="Root of original UI_Map_* UModel exports")
    parser.add_argument("--items", type=Path, required=True, help="Original UI_ItemIcons_Large Texture2D exports")
    parser.add_argument("--portraits", type=Path, required=True, help="Original UI_portraits_objectives Texture2D exports")
    args = parser.parse_args()
    if args.check:
        inputs, _, _ = original_inputs(args.textures, args.fonts, args.maps, args.items, args.portraits)
        print(json.dumps({"scope": "Read-only original asset inspection; no files staged", **inspect_inputs(inputs)}))
    elif args.output is None:
        parser.error("output is required unless --check is used")
    else:
        prepare(args.textures, args.fonts, args.maps, args.items, args.portraits, args.output)
