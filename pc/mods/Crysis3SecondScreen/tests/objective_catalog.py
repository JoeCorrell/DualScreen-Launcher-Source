"""Private objective-localization build tool, not an installed runtime adapter.

Run: python objective_catalog.py SOURCE_XML EXTRACTION_EVIDENCE NEW_OUTPUT_JSON
Output contains extracted game strings and belongs in private .work, not source.
"""
import hashlib
import json
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET
import zlib

SOURCE_SHA256 = "e9e4201461bb0d28c53a4bd768e59bbf52f4c22cfb483d1e558e85791ae1c668"
ARCHIVE_SHA256 = "d288fd8c329bc7e46f09e6a7c42e348b41070fc33e94b5e3158d8a176cbcbb9e"
ENTRIES_SHA256 = "6a0eaea3544b95dc65f25346e24f2c4d4ee00ae079ccdf0fb25f4aaf64a8ae9e"
NS = "{urn:schemas-microsoft-com:office:spreadsheet}"


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("Duplicate JSON key")
        result[key] = value
    return result


def parse_rows(source):
    if len(source) > 2_000_000 or b"<!DOCTYPE" in source.upper() or b"<!ENTITY" in source.upper():
        raise ValueError("Unsupported XML size or declarations")
    root = ET.fromstring(source)
    worksheets = root.findall(NS + "Worksheet")
    if len(worksheets) != 1:
        raise ValueError("Expected exactly one worksheet")
    rows = worksheets[0].findall(NS + "Table/" + NS + "Row")
    columns = None
    entries = {}
    for row in rows:
        cells = {}
        index = 1
        for cell in row.findall(NS + "Cell"):
            index = int(cell.get(NS + "Index", str(index)))
            if not 1 <= index <= 16 or index in cells:
                raise ValueError("Invalid spreadsheet column")
            data = cell.find(NS + "Data")
            cells[index] = "" if data is None else "".join(data.itertext())
            index += 1
        if columns is None:
            names = {value: index for index, value in cells.items() if value}
            if "KEY" not in names or "TRANSLATED TEXT" not in names:
                raise ValueError("Missing localization column headers")
            columns = names
            continue
        key = cells.get(columns["KEY"], "")
        if not key:
            continue  # Description/formatting rows have no key.
        text = cells.get(columns["TRANSLATED TEXT"], "")
        if not re.fullmatch(r"[A-Za-z0-9_]+", key) or len(key.encode()) >= 192:
            raise ValueError("Invalid localization key")
        if not text or len(text.encode("utf-8")) > 4096 or "\0" in text:
            raise ValueError("Invalid translated text")
        if key in entries:
            raise ValueError("Duplicate localization key")
        entries[key] = text
    if not entries or len(entries) > 4096:
        raise ValueError("Invalid catalog entry count")
    return entries


def build(source_path, evidence_path):
    source = Path(source_path).read_bytes()
    if hashlib.sha256(source).hexdigest() != SOURCE_SHA256:
        raise ValueError("Objective XML does not match inspected English source")
    evidence = json.loads(Path(evidence_path).read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)
    files = [item for item in evidence.get("files", []) if item.get("name") == "text_ui_objectives.xml"]
    if evidence.get("sha256") != ARCHIVE_SHA256 or evidence.get("allEntryCrcsValid") is not True or len(files) != 1:
        raise ValueError("Extraction provenance does not match")
    if files[0].get("bytes") != len(source) or files[0].get("crc") != f"{zlib.crc32(source) & 0xffffffff:08x}":
        raise ValueError("Extraction entry length/CRC mismatch")
    entries = parse_rows(source)
    if hashlib.sha256(canonical(entries)).hexdigest() != ENTRIES_SHA256:
        raise ValueError("Parsed catalog differs from inspected canonical translation set")
    return {"schema": 1, "language": "English", "sourceSha256": SOURCE_SHA256,
            "archiveSha256": ARCHIVE_SHA256, "archiveSignatureVerified": False,
            "entriesSha256": hashlib.sha256(canonical(entries)).hexdigest(), "entries": entries}


def load(source):
    if len(source) > 2_000_000:
        raise ValueError("Oversize localization catalog")
    catalog = json.loads(source, object_pairs_hook=no_duplicates)
    if catalog.get("schema") != 1 or catalog.get("language") != "English" or catalog.get("sourceSha256") != SOURCE_SHA256 or catalog.get("archiveSha256") != ARCHIVE_SHA256:
        raise ValueError("Unsupported localization provenance")
    entries = catalog.get("entries")
    if not isinstance(entries, dict) or not 1 <= len(entries) <= 4096 or any(not isinstance(k, str) or not isinstance(v, str) or not k or not v or len(v.encode("utf-8")) > 4096 for k, v in entries.items()):
        raise ValueError("Malformed localization entries")
    if catalog.get("entriesSha256") != ENTRIES_SHA256 or hashlib.sha256(canonical(entries)).hexdigest() != ENTRIES_SHA256:
        raise ValueError("Localization content digest mismatch")
    return catalog


def resolve(catalog, key, confirmed_language):
    # The existence of an English asset does not establish current game language.
    if confirmed_language != "English" or not isinstance(key, str):
        return None
    return catalog["entries"].get(key[1:] if key.startswith("@") else key)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Expected source XML, extraction evidence, NEW output JSON")
    result = build(sys.argv[1], sys.argv[2])
    with Path(sys.argv[3]).open("xb") as output:
        output.write(canonical(result) + b"\n")
    print(f"Built {len(result['entries'])} verified English entries; runtime language remains unverified.")
