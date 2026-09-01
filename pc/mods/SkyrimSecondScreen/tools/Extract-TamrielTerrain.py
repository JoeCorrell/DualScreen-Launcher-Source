"""Extract Skyrim's authored Tamriel LOD16 meshes and build a height resource."""

from __future__ import annotations

import argparse
import importlib.util
import re
import subprocess
import sys
import tempfile
from pathlib import Path


_map_script = Path(__file__).with_name("Extract-TamrielMap.py")
_spec = importlib.util.spec_from_file_location("extract_tamriel_map", _map_script)
if _spec is None or _spec.loader is None:
    raise RuntimeError(f"could not load {_map_script}")
_map_module = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = _map_module
_spec.loader.exec_module(_map_module)
extract_record = _map_module.extract_record
read_records = _map_module.read_records


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", required=True)
    parser.add_argument("--device", required=True)
    parser.add_argument("--archive", required=True)
    parser.add_argument("--builder", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    archive_flags, records = read_records(args.adb, args.device, args.archive)
    pattern = re.compile(r"(?:^|\\)(tamriel\.16\.-?\d+\.-?\d+\.btr)$", re.I)
    terrain = []
    for record in records:
        match = pattern.search(record.path)
        if match:
            terrain.append((record, match.group(1)))
    if not terrain:
        raise RuntimeError("no Tamriel LOD16 terrain meshes found")

    with tempfile.TemporaryDirectory(prefix="skyrim-tamriel-") as temporary:
        directory = Path(temporary)
        for index, (record, name) in enumerate(terrain, 1):
            (directory / name).write_bytes(
                extract_record(args.adb, args.device, args.archive,
                               archive_flags, record)
            )
            if index == 1 or index % 16 == 0 or index == len(terrain):
                print(f"extracted terrain {index}/{len(terrain)}", flush=True)
        subprocess.run(
            [args.builder, str(directory), args.output], check=True
        )


if __name__ == "__main__":
    main()
