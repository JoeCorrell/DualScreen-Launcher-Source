"""Read tagged actor/tweak properties from locally decompressed Dishonored 801/30 packages.

Read-only research tool, not runtime code. Preserves property byte offsets and
unknown payloads. Does not infer default properties, discovery or map coordinates.
Requires an already decompressed package; never modifies the game or stages assets.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path


class Package:
    def __init__(self, path):
        self.path = Path(path)
        if not 0 < self.path.stat().st_size <= 512 * 1024 * 1024:
            raise ValueError("Package exceeds the bounded research reader")
        self.data = self.path.read_bytes()
        if self.unpack("IHH", 0) != (0x9E2A83C1, 801, 30):
            raise ValueError("Only the inspected Dishonored package version is supported")
        # Summary begins with tag, version/licensee, header size, folder FString.
        _, cursor = self.string(12)
        self.flags, = self.unpack("I", cursor)
        generations, = self.unpack("i", cursor + 64)
        if not 0 <= generations <= 256:
            raise ValueError("Invalid summary generation count")
        compression, chunks = self.unpack("II", cursor + 76 + generations * 12)
        if compression or chunks:
            raise ValueError("Decompress the local analysis copy before reading its tables")
        nc, no, ec, eo, ic, io = self.unpack("6i", cursor + 4)
        if not all(0 <= count <= 262144 for count in (nc, ec, ic)):
            raise ValueError("Invalid package table count")
        self.names = []
        for _ in range(nc):
            value, no = self.string(no)
            self.span(no, 8)
            no += 8
            self.names.append(value)
        self.imports = []
        for _ in range(ic):
            package, kind = self.name(io), self.name(io + 8)
            outer, = self.unpack("i", io + 16)
            self.imports.append({"name": self.name(io + 20), "outer": outer, "class": kind, "package": package})
            io += 28
        self.exports = []
        for i in range(ec):
            cls, supertype, outer = self.unpack("3i", eo)
            name = self.name(eo + 12)
            archetype, = self.unpack("i", eo + 20)
            high, low, size, offset, export_flags, generations = self.unpack("IIiiIi", eo + 24)
            if not 0 <= generations <= 256:
                raise ValueError("Invalid export generation count")
            self.span(offset, size)
            row = {"index": i + 1, "name": name, "classIndex": cls, "super": supertype, "outer": outer,
                   "archetype": archetype, "flags": (high << 32) | low, "serialOffset": offset, "serialSize": size}
            self.exports.append(row)
            eo += 68 + generations * 4
        for row in self.exports:
            row["class"] = self.reference(row["classIndex"]) if row["classIndex"] else "Class"

    def span(self, offset, length):
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise ValueError(f"Package span outside file: {offset:x}+{length:x}")

    def unpack(self, fmt, offset):
        self.span(offset, struct.calcsize("<" + fmt))
        return struct.unpack_from("<" + fmt, self.data, offset)

    def string(self, offset):
        count, = self.unpack("i", offset)
        if abs(count) > 65536:
            raise ValueError("Unbounded FString")
        start = offset + 4
        length = abs(count) * (2 if count < 0 else 1)
        self.span(start, length)
        text = self.data[start:start + length].decode("utf-16-le" if count < 0 else "latin1")
        if count and (not text.endswith("\0") or "\0" in text[:-1]):
            raise ValueError("Malformed FString terminator")
        return text[:-1] if count else "", start + length

    def name(self, offset):
        index, number = self.unpack("ii", offset)
        if not 0 <= index < len(self.names) or number < 0:
            raise ValueError(f"Invalid FName at {offset:x}")
        return self.names[index] + (f"_{number - 1}" if number else "")

    def reference(self, index):
        table = self.exports if index > 0 else self.imports
        item = index - 1 if index > 0 else -index - 1
        if not index:
            return None
        if not 0 <= item < len(table):
            raise ValueError(f"Invalid package object reference {index}")
        return table[item]["name"]

    def properties(self, row):
        offset, end = row["serialOffset"], row["serialOffset"] + row["serialSize"]
        if row["flags"] & 0x02000000:
            # FStateFrame in 801: node, state, 32-bit probe, 16-bit latent,
            # array of pushed (state,node,offset) records, optional node offset.
            node, = self.unpack("i", offset)
            count, = self.unpack("i", offset + 14)
            if not 0 <= count <= 256:
                raise ValueError("Invalid pushed-state count")
            offset += 18 + count * 12 + (4 if node else 0)
        if row["class"] == "StaticMeshComponent":
            # UObject::DeserializeTemplate: owner class, then template name
            # only when this object or an outer is a class-default object.
            offset += 4
            owner, visited = row, set()
            for _ in range(64):
                if owner["index"] in visited:
                    raise ValueError("Cycle in component outer chain")
                visited.add(owner["index"])
                if owner["flags"] & (0x200 << 32):
                    offset += 8
                    break
                if not owner["outer"]:
                    break
                if not 0 < owner["outer"] <= len(self.exports):
                    raise ValueError("External component template outer is unsupported")
                owner = self.exports[owner["outer"] - 1]
            else:
                raise ValueError("Component outer chain limit reached")
        offset += 4  # UObject NetIndex.
        fields = []
        for _ in range(4096):
            tag = offset
            name = self.name(offset)
            offset += 8
            if name == "None":
                if offset > end:
                    raise ValueError("Property terminator crosses export")
                return fields, offset
            kind = self.name(offset)
            size, index = self.unpack("ii", offset + 8)
            offset += 16
            extra = None
            if kind in ("StructProperty", "ByteProperty"):
                extra = self.name(offset)
                offset += 8
            if kind == "BoolProperty":
                extra, = self.unpack("B", offset)
                offset += 1
                if extra not in (0, 1) or size != 0:
                    raise ValueError("Invalid BoolProperty")
            if size < 0 or index < 0 or offset + size > end:
                raise ValueError(f"Invalid property size: {name}")
            value = None
            if kind in ("ObjectProperty", "ComponentProperty", "ClassProperty") and size == 4:
                ref, = self.unpack("i", offset)
                value = {"index": ref, "object": self.reference(ref)}
            elif kind == "IntProperty" and size == 4:
                value, = self.unpack("i", offset)
            elif kind == "FloatProperty" and size == 4:
                value, = self.unpack("f", offset)
            elif kind == "NameProperty" and size == 8:
                value = self.name(offset)
            elif kind == "ByteProperty" and size == 8:
                value = self.name(offset)
            elif kind == "ByteProperty" and size == 1:
                value, = self.unpack("B", offset)
            elif kind == "BoolProperty":
                value = bool(extra)
            elif kind == "StrProperty":
                value, finish = self.string(offset)
                if finish != offset + size:
                    raise ValueError("FString property length mismatch")
            elif kind == "StructProperty" and extra in ("Vector", "Rotator") and size == 12:
                value = self.unpack("3f" if extra == "Vector" else "3i", offset)
            if isinstance(value, float) and not math.isfinite(value):
                raise ValueError("Nonfinite scalar")
            fields.append({"name": name, "kind": kind, "arrayIndex": index, "type": extra,
                           "tagOffset": tag, "dataOffset": offset, "size": size, "value": value,
                           "raw": self.data[offset:offset + size].hex() if value is None and size <= 128 else None})
            offset += size
        raise ValueError("Property tag limit reached")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--class", dest="classes", action="append", required=True)
    args = parser.parse_args()
    package = Package(args.package)
    rows, errors = [], []
    for export in package.exports:
        if export["class"] not in args.classes:
            continue
        try:
            fields, native = package.properties(export)
            rows.append({**export, "properties": fields, "nativeDataOffset": native,
                         "nativeDataSize": export["serialOffset"] + export["serialSize"] - native})
        except (ValueError, UnicodeError, struct.error) as error:
            errors.append({"index": export["index"], "name": export["name"], "error": str(error)})
    report = {"scope": "Static serialized values only. Absent properties inherit defaults; no player discovery or calibration implied.",
              "package": str(args.package), "sha256": hashlib.sha256(package.data).hexdigest(),
              "exports": len(package.exports), "objects": rows, "errors": errors}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(f"Read {len(rows)} objects; {len(errors)} retained errors; no game files changed")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
