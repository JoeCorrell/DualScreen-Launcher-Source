"""Mechanically emit the private, provenance-pinned English panel catalog."""
from pathlib import Path
import json
import hashlib
import re
import sys
import xml.etree.ElementTree as ET
import zlib

import objective_catalog

MESSAGES_SHA256 = "608a7037a7f6e11feb76b29970365b2e425bdd93bbed0aeee9678063a651d96e"
UI_SHA256 = "22ce1e93b615fbb2eadeed231e32576f006680f68a5213d78b7d7fa5aaf48d1c"
PATCH_SHA256 = "f17b52cc44ddb4cc320fc882cb7109a4f11b3bb7afadb3ac79706e8138949d59"
UI_ARCHIVE_SHA256 = "1b4ac212532957b6a94626db9ff60aaf7607a39b7dd371fd9cfd96e22448f719"
PATCH_ARCHIVE_SHA256 = "08e06ea600a582044ace09958bd8c46189b7335b8b7dffcc55ab1e86a555bb3c"


def verified_entry(folder, name, digest, archive):
    data = (folder / name).read_bytes()
    evidence = json.loads((folder / "_extraction-evidence.json").read_text(encoding="utf-8"), object_pairs_hook=objective_catalog.no_duplicates)
    matches = [entry for entry in evidence.get("files", []) if entry.get("name") == name]
    if hashlib.sha256(data).hexdigest() != digest or evidence.get("sha256") != archive or evidence.get("allEntryCrcsValid") is not True or len(matches) != 1:
        raise ValueError("Level source provenance mismatch")
    if matches[0].get("bytes") != len(data) or matches[0].get("crc") != f"{zlib.crc32(data) & 0xffffffff:08x}":
        raise ValueError("Level source entry length/CRC mismatch")
    return data


def level_entries(extracted):
    messages = verified_entry(extracted / "English", "text_ui_messages.xml", MESSAGES_SHA256, objective_catalog.ARCHIVE_SHA256)
    base = verified_entry(extracted / "UI", "Libs/UI/config/navigation_singleplayer.xml", UI_SHA256, UI_ARCHIVE_SHA256)
    patch = verified_entry(extracted / "Patch", "C3/Libs/UI/config/navigation_singleplayer.xml", PATCH_SHA256, PATCH_ARCHIVE_SHA256)
    if len(messages) > 3_000_000 or b"<!DOCTYPE" in messages.upper() or b"<!ENTITY" in messages.upper():
        raise ValueError("Unsupported messages XML")
    # The exact messages workbook has Sheet1 plus a blank Sheet2. Select the
    # named data sheet and the actual translated column, never original text.
    ns = objective_catalog.NS
    sheets = [sheet for sheet in ET.fromstring(messages).findall(ns + "Worksheet") if sheet.get(ns + "Name") == "Sheet1"]
    if len(sheets) != 1:
        raise ValueError("Expected unique messages data worksheet")
    selected = {}
    columns = None
    for row in sheets[0].findall(ns + "Table/" + ns + "Row"):
        cells = {}
        index = 1
        for cell in row.findall(ns + "Cell"):
            index = int(cell.get(ns + "Index", str(index)))
            if not 1 <= index <= 16 or index in cells:
                raise ValueError("Invalid message spreadsheet column")
            data = cell.find(ns + "Data")
            cells[index] = "" if data is None else "".join(data.itertext())
            index += 1
        if columns is None:
            columns = {value: key for key, value in cells.items() if value}
            if "KEY" not in columns or "TRANSLATED TEXT" not in columns:
                raise ValueError("Missing message column headers")
            continue
        key = cells.get(columns["KEY"], "")
        if not (key.startswith("menu_levelname_") or re.fullmatch(r"ui_level_[a-z0-9_]+_location", key)):
            continue
        value = cells.get(columns["TRANSLATED TEXT"], "")
        if key in selected or not value or "\0" in value or len(value.encode("utf-8")) >= 192:
            raise ValueError("Invalid selected level translation")
        selected[key] = value
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
    from decode_xml import decode  # Existing bounded CryXmlB decoder; no game scripts executed.

    def mappings(source):
        result = {}
        for node in decode(source).iter():
            caption, parameter = node.get("caption", ""), node.get("param", "")
            if not caption.startswith("@menu_levelname_") or not re.fullmatch(r"[A-Za-z0-9_]+", parameter) or node.get("action") not in ("difficulty_screen", "goto_attract_mode"):
                continue
            level = parameter.lower()
            if level in result and result[level] != caption:
                raise ValueError("Conflicting level caption mapping")
            result[level] = caption
        return result

    levels = mappings(base)
    if len(levels) != 8 or mappings(patch) != levels:
        raise ValueError("Base/patch level caption contracts disagree")
    result = []
    for level, caption in sorted(levels.items()):
        title = selected.get(caption[1:])
        if title is None:
            raise ValueError("Mapped level caption has no English translation")
        location_key = "ui_level_" + level + "_location"
        location = selected.get(location_key, "")
        # The source includes escaped line markers AND actual newlines. Fold
        # these display breaks only; retain all mission/location/date wording.
        location = " / ".join(line.strip() for line in location.replace("\\\\n", "\n").replace("\\n", "\n").splitlines() if line.strip())
        if len(location.encode("utf-8")) >= 192:
            raise ValueError("Formatted location does not fit bounded text")
        result.append((level, caption, title, "@" + location_key if location else "", location))
    return result


def header(source, evidence):
    catalog = objective_catalog.build(source, evidence)
    entries = sorted(catalog["entries"].items())
    levels = level_entries(Path(source).resolve().parent.parent)
    if len(entries) != 298 or any(len(k.encode("utf-8")) >= 192 or len(v.encode("utf-8")) >= 192 for k, v in entries):
        raise ValueError("Catalog does not fit native bounded text fields")
    lines = ["#pragma once", "// Generated by tests/NativeObjectiveCatalog.py from the exact private game asset.",
             "// English is the panel's selected translation, NOT inferred engine language.",
             "// Archive signature was not verified; extraction CRCs and these digests were.",
             "#include <cstddef>", "#include <string_view>", "namespace c3::objective_catalog {",
             'inline constexpr char SourceSha256[]="' + catalog["sourceSha256"] + '";',
             'inline constexpr char ArchiveSha256[]="' + catalog["archiveSha256"] + '";',
             'inline constexpr char EntriesSha256[]="' + catalog["entriesSha256"] + '";',
             "struct Entry { std::string_view key, english; };", "inline constexpr Entry Entries[]{"]
    for key, value in entries:
        lines.append("    {" + json.dumps(key, ensure_ascii=True) + "," + json.dumps(value, ensure_ascii=True) + "},")
    lines.extend(["};", "inline constexpr size_t Count=sizeof(Entries)/sizeof(Entries[0]);", "static_assert(Count==298);",
                  'inline constexpr char MessagesSha256[]="' + MESSAGES_SHA256 + '";',
                  'inline constexpr char NavigationSha256[]="' + UI_SHA256 + '";',
                  'inline constexpr char PatchNavigationSha256[]="' + PATCH_SHA256 + '";',
                  'inline constexpr char UiArchiveSha256[]="' + UI_ARCHIVE_SHA256 + '";',
                  'inline constexpr char PatchArchiveSha256[]="' + PATCH_ARCHIVE_SHA256 + '";',
                  "// Base and patched navigation agree on these level/caption mappings.",
                  "// Location text only folds source line breaks into separators; no geography inferred.",
                  "struct LevelEntry { std::string_view level, titleKey, title, locationKey, location; };",
                  "inline constexpr LevelEntry Levels[]{"])
    for entry in levels:
        lines.append("    {" + ",".join(json.dumps(value, ensure_ascii=True) for value in entry) + "},")
    lines.extend(["};", "inline constexpr size_t LevelCount=sizeof(Levels)/sizeof(Levels[0]);",
                  "static_assert(LevelCount==8);", "} // namespace c3::objective_catalog", ""])
    return "\n".join(lines)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Expected exact source XML, extraction evidence, output header")
    output = header(sys.argv[1], sys.argv[2])
    Path(sys.argv[3]).write_text(output, encoding="utf-8", newline="\n")
    print("Generated 298 objective and 8 level/location English panel entries with pinned provenance")
