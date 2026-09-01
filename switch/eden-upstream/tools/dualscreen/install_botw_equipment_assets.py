#!/usr/bin/env python3



from __future__ import annotations

import argparse
import json
import shutil
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    parser.add_argument("--player-assets", type=Path, required=True)
    parser.add_argument(
        "--replace-existing",
        action="store_true",
        help="Replace GLBs already present in the Android asset directory",
    )
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    if manifest.get("errors"):
        raise ValueError("equipment manifest contains unresolved actor errors")
    index_path = args.player_assets / "index.json"
    index = json.loads(index_path.read_text(encoding="utf-8"))
    actors = index.setdefault("actors", {})

    copied = 0
    preserved = 0
    for model_name in sorted(manifest["models"]):
        source = args.build_root / model_name / f"{model_name}.glb"
        destination = args.player_assets / f"{model_name}.glb"
        if not source.is_file():
            raise FileNotFoundError(source)
        if destination.exists() and not args.replace_existing:
            preserved += 1
            continue
        shutil.copy2(source, destination)
        copied += 1

    for actor, entry in sorted(manifest["actors"].items()):
        model_file = args.player_assets / entry["file"]
        if not model_file.is_file():
            raise FileNotFoundError(f"{actor} references missing {model_file}")
        actors[actor] = entry

    index_path.write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(
        f"Installed {copied} GLBs, preserved {preserved}, and merged "
        f"{len(manifest['actors'])} actor mappings"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
