#!/usr/bin/env python3
"""Export locally extracted LA tables without inventing progression or item IDs.

The output is private development input, not a redistributable asset pack.
Requires oead. This preserves condition expressions verbatim: callers must trace
their native evaluator, not assume all conditions are simple flag ANDs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import oead

from la_msbt import decode as decode_msbt_text


def plain(value):
    if isinstance(value, oead.gsheet.Struct):
        return {key: plain(child) for key, child in value.items()}
    if isinstance(value, (str, bool, int, float)) or value is None:
        return value
    return [plain(child) for child in value]


def unique_index(rows, field: str):
    result = {}
    for row in rows:
        key = row[field]
        if key in result:
            raise ValueError(f"Duplicate {field}: {key}")
        result[key] = row
    return result


def export(romfs: Path, output: Path):
    root = romfs / "region_common" / "datasheets"
    required = (
        "Items", "GlobalFlags", "Conditions", "UiFieldMapMask",
        "UiFieldMapIcons", "UiFieldMapLocationName", "UiDungeonArea",
        "UiDungeonIcon", "UiDungeonStair", "WarpPoint", "TelephoneHint",
        "UiMemory", "UiMemoryHearts", "UiMemoryShells", "LocationName",
    )
    tables, sources = {}, {}
    for name in required:
        source = root / f"{name}.gsheet"
        data = source.read_bytes()
        sheet = oead.gsheet.parse(data)
        if sheet.name != name:
            raise ValueError(f"Sheet name mismatch: {source}")
        tables[name] = plain(sheet.values)
        sources[source.relative_to(romfs).as_posix()] = hashlib.sha256(data).hexdigest()

    flags = unique_index(tables["GlobalFlags"], "symbol")
    unique_index(tables["GlobalFlags"], "index")
    unique_index(tables["Items"], "itemID")
    unique_index(tables["Items"], "symbol")
    for table, fields in (
        ("UiFieldMapMask", ("mMaskFlagName", "mShowFlagName")),
        ("UiFieldMapIcons", ("mFirstShowFlagName",)),
    ):
        for row in tables[table]:
            for field in fields:
                name = row[field]
                if name and name not in flags:
                    raise ValueError(f"Unknown discovery flag {table}.{field}: {name}")

    messages = {}
    for source in sorted((romfs / "regionUS" / "USen" / "message").glob("*.msbt")):
        if source.stem not in {"UI", "Glossary", "Place", "Hint", "Telephone"}:
            continue
        data = source.read_bytes()
        messages[source.stem] = decode_msbt_text(data)
        sources[source.relative_to(romfs).as_posix()] = hashlib.sha256(data).hexdigest()
    if not {"UI", "Glossary", "Place", "Telephone"} <= messages.keys():
        raise ValueError("Missing locally extracted US English message tables")

    result = {"schema": 1, "source": "locally extracted base RomFS",
              "runtimeValidated": False, "patchedResourcesVerified": False,
              "sourceSha256": sources, "tables": tables, "messages": messages}
    # Parse and validate everything before creating an output; an incomplete
    # extraction must not quietly replace the last useful metadata export.
    encoded = json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(encoded + "\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("romfs", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = export(args.romfs, args.output)
    for name, rows in result["tables"].items():
        print(f"{name}: {len(rows)} rows")
    print(f"Verified {len(result['sourceSha256'])} source hashes; wrote {args.output}")


if __name__ == "__main__":
    main()
