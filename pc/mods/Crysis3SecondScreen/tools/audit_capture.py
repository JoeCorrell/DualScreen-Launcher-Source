"""Validate read-only runtime evidence; never authorize hooks or claim decryption.

Only the two locally verified Crysis builds are accepted. Files are read from a
fixed-name capture directory, never from paths supplied by its manifest. JSON
output goes to stdout; this tool does not modify the capture or executable.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import struct
import sys

SUPPORTED = frozenset({
    "f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e",
    "fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055",
})
FAULT_RVA = 0x02C169CE
OBSERVED_HEADER_SHA = "fb530657a66ea51a6a7495c36085e0c856d6daccffbacc1db8c037d48c55e25d"
TRIAL_SHA = "fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055"
MAX_IMAGE = 128 * 1024 * 1024


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def integer(value, minimum, maximum, label):
    require(type(value) is int and minimum <= value <= maximum, f"Invalid {label}")
    return value


def read_bounded(path, limit):
    require(not path.is_symlink() and path.is_file(), f"Missing/non-regular file: {path.name}")
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"Oversized file: {path.name}")
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"Duplicate JSON field: {key}")
        result[key] = value
    return result


def parse_json(data):
    return json.loads(data, object_pairs_hook=unique_object)


def pe_layout(data):
    require(len(data) >= 64 and data[:2] == b"MZ", "Missing DOS header")
    pe = struct.unpack_from("<I", data, 60)[0]
    require(64 <= pe <= 4096 and pe + 248 <= len(data), "Truncated PE header")
    require(data[pe:pe + 4] == b"PE\0\0", "Missing PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    require(machine == 0x14C and 1 <= count <= 32 and optional_size == 224,
            "Unexpected PE architecture/section count")
    optional = pe + 24
    require(struct.unpack_from("<H", data, optional)[0] == 0x10B, "Expected PE32")
    image_size, headers_size = struct.unpack_from("<II", data, optional + 56)
    table = optional + optional_size
    require(table + count * 40 <= headers_size <= min(len(data), 65536), "Invalid header size")
    require(headers_size <= image_size <= MAX_IMAGE, "Invalid image size")
    sections = {}
    ranges = []
    for index in range(count):
        offset = table + index * 40
        name = data[offset:offset + 8].rstrip(b"\0").decode("ascii")
        size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, offset + 8)
        flags = struct.unpack_from("<I", data, offset + 36)[0]
        require(name and name not in sections, "Duplicate/empty PE section")
        require(headers_size <= rva < image_size and rva + size <= image_size, "Invalid section range")
        if size:
            ranges.append((rva, rva + size))
        sections[name] = dict(rva=rva, size=size, rawSize=raw_size, rawOffset=raw_offset, flags=flags)
    ranges.sort()
    require(all(a[1] <= b[0] for a, b in zip(ranges, ranges[1:])), "Overlapping PE sections")
    return dict(imageSize=image_size, headersSize=headers_size, sections=sections)


def region_inventory(data, base, image_size):
    lines = data.splitlines()
    require(1 <= len(lines) <= 65536, "Invalid memory region count")
    cursor, end = base, base + image_size
    fault_region = None
    for line in lines:
        region = parse_json(line)
        require(isinstance(region, dict), "Memory region must be an object")
        start = integer(region.get("base"), 0, 0xFFFFFFFF, "region base")
        size = integer(region.get("bytes"), 1, 0x100000000, "region size")
        for key in ("allocationBase", "state", "protect", "allocationProtect", "type"):
            integer(region.get(key), 0, 0xFFFFFFFF, f"region {key}")
        require(cursor < end and start <= cursor < start + size <= 0x100000000,
                "Gapped, overlapping or out-of-order region inventory")
        require(cursor == base or start == cursor, "Overlapping memory regions")
        if start <= base + FAULT_RVA < start + size:
            fault_region = region
        cursor = start + size
    require(cursor >= end and fault_region is not None, "Incomplete memory region coverage")
    return fault_region


def entropy(data):
    return round(-sum((n / len(data)) * math.log2(n / len(data)) for n in Counter(data).values()), 5)


def audit(directory, source):
    original = read_bounded(source, MAX_IMAGE)
    source_hash = digest(original)
    require(source_hash in SUPPORTED, "Unrecognized executable; evidence refused")
    disk = pe_layout(original)
    manifest = parse_json(read_bounded(directory / "capture.json", 65536))
    require(isinstance(manifest, dict) and type(manifest.get("schema")) is int and manifest["schema"] == 2,
            "Expected capture schema 2")
    require(manifest.get("sourceSha256") == source_hash, "Capture/source build mismatch")
    for field in ("complete", "memoryRegionsComplete"):
        require(manifest.get(field) is True, f"Capture is not complete: {field}")
    for field in ("atomicSnapshot", "decryptionVerified", "nativeHooksVerified"):
        require(manifest.get(field) is False, f"Unsupported evidence claim: {field}")
    integer(manifest.get("pid"), 1, 0xFFFFFFFF, "PID")
    integer(manifest.get("durationMs"), 0, 0xFFFFFFFFFFFFFFFF, "capture duration")
    base = integer(manifest.get("imageBase"), 1, 0xFFFFFFFF, "image base")
    require(base + disk["imageSize"] <= 0x100000000, "Image address overflow")

    def blob(name, entry, size):
        require(isinstance(entry, dict) and entry.get("complete") is True, f"Incomplete {name}")
        require(type(entry.get("capturedBytes")) is int and entry["capturedBytes"] == size,
                f"Incorrect declared size: {name}")
        data = read_bounded(directory / name, size)
        require(len(data) == size and digest(data) == entry.get("sha256"), f"Size/hash mismatch: {name}")
        return data

    headers = blob("loaded-headers.bin", manifest.get("headers"), disk["headersSize"])
    loaded = pe_layout(headers)
    observed = (manifest.get("layoutProfile") == "observed-20260907-5section" and
                source_hash == TRIAL_SHA and digest(headers) == OBSERVED_HEADER_SHA and
                loaded["imageSize"] == 0x052a1000 and disk["imageSize"] == 0x052a3000 and
                manifest.get("moduleBytes") == disk["imageSize"])
    require(observed or manifest.get("layoutProfile", "disk-layout") == "disk-layout", "Unrecognized live layout fingerprint")
    require((observed or loaded["imageSize"] == disk["imageSize"]) and loaded["headersSize"] == disk["headersSize"],
            "Loaded/disk image size mismatch")
    expected_names = set(disk["sections"]) - ({".IkWOkkX"} if observed else set())
    require(set(loaded["sections"]) == expected_names, "Loaded/disk section set mismatch")
    for name, section in disk["sections"].items():
        if observed and name == ".IkWOkkX":
            continue
        require(all(section[key] == loaded["sections"][name][key] for key in ("rva", "size")),
                f"Loaded/disk section layout mismatch: {name}")
    text, rdata, fault = (disk["sections"][name] for name in (".text", ".rdata", ".data"))
    require(0 < text["size"] <= 64 * 1024 * 1024 and text["flags"] & 0x20, "Invalid text section")
    require(0 < rdata["size"] <= 8 * 1024 * 1024, "Invalid rdata section")
    require(fault["rva"] <= FAULT_RVA < fault["rva"] + fault["size"], "Fault outside data section")
    require(manifest.get("sectionRva") == text["rva"] and manifest.get("sectionVirtualBytes") == text["size"],
            "Text capture location mismatch")
    captured = blob("loaded-text.bin", dict(complete=True, capturedBytes=manifest.get("capturedBytes"),
                    sha256=manifest.get("captureSha256")), text["size"])
    for name, key, rva, size in (("loaded-rdata.bin", "rdata", rdata["rva"], rdata["size"]),
                                 ("loaded-fault-page.bin", "faultPage", FAULT_RVA & ~4095, 4096)):
        entry = manifest.get(key)
        require(isinstance(entry, dict) and entry.get("rva") == rva, f"Location mismatch: {key}")
        if key == "rdata":
            require(entry.get("virtualBytes") == size, "Rdata virtual size mismatch")
        blob(name, entry, size)
    regions = read_bounded(directory / "memory-regions.jsonl", 32 * 1024 * 1024)
    require(digest(regions) == manifest.get("memoryRegionsSha256"), "Missing/mismatched memory region hash")
    observed = region_inventory(regions, base, disk["imageSize"])
    count = min(text["size"], text["rawSize"])
    require(count > 0 and text["rawOffset"] + text["rawSize"] <= len(original), "Invalid disk text range")
    disk_text = original[text["rawOffset"]:text["rawOffset"] + count]
    return dict(schema=1, evidenceIntegrityChecked=True, sourceSha256=source_hash,
                layoutProfile=manifest.get("layoutProfile", "disk-layout"),
                imageBase=base, textRva=text["rva"], comparedTextBytes=count,
                differingTextBytes=sum(a != b for a, b in zip(disk_text, captured)),
                diskTextEntropy=entropy(disk_text), capturedTextEntropy=entropy(captured),
                faultAddress=base + FAULT_RVA, faultRegion=observed,
                faultRegionExecutableAtInventory=observed["state"] == 0x1000 and
                not (observed["protect"] & 0x100) and (observed["protect"] & 0xFF) in (0x10, 0x20, 0x40, 0x80),
                atomicSnapshot=False, decryptionVerified=False, nativeHooksVerified=False,
                limitation="Checks consistency, not authenticity, decryption, stable gameplay or hook safety; region inventory predates byte copies.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("source_executable", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(audit(args.capture_directory, args.source_executable), indent=2))
    except (OSError, ValueError, KeyError, TypeError, struct.error) as error:
        print(f"Capture refused: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
