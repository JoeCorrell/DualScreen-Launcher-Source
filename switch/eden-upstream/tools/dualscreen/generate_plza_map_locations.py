#!/usr/bin/env python3

from __future__ import annotations

import argparse
import struct
from pathlib import Path


class FlatBuffer:
    def __init__(self, data: bytes):
        self.data = data

    def u16(self, offset: int) -> int:
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        return struct.unpack_from("<I", self.data, offset)[0]

    def i32(self, offset: int) -> int:
        return struct.unpack_from("<i", self.data, offset)[0]

    def root(self) -> int:
        return self.u32(0)

    def field(self, table: int, index: int) -> int | None:
        vtable = table - self.i32(table)
        entry = vtable + 4 + index * 2
        if entry + 2 > vtable + self.u16(vtable):
            return None
        relative = self.u16(entry)
        return table + relative if relative else None

    def indirect(self, offset: int) -> int:
        return offset + self.u32(offset)

    def string(self, field: int | None) -> str:
        if field is None:
            return ""
        start = self.indirect(field)
        size = self.u32(start)
        return self.data[start + 4 : start + 4 + size].decode("utf-8", "replace")

    def vector(self, field: int | None) -> tuple[int, int]:
        if field is None:
            return 0, 0
        start = self.indirect(field)
        return start + 4, self.u32(start)

    def byte_vector(self, field: int | None) -> bytes:
        start, count = self.vector(field)
        return self.data[start : start + count]


def entries(buffer: FlatBuffer, table: int):
    start, count = buffer.vector(buffer.field(table, 2))
    for index in range(count):
        yield buffer.indirect(start + index * 4)


def scene_entries(buffer: FlatBuffer, table: int):
    entry_type = buffer.string(buffer.field(table, 0))
    payload = buffer.byte_vector(buffer.field(table, 1))
    if entry_type == "trinity_SceneObject":
        yield payload
    elif entry_type == "trinity_ObjectTemplate":
        try:
            wrapped = FlatBuffer(payload)
            root = wrapped.root()
            if wrapped.string(wrapped.field(root, 4)) == "trinity_SceneObject":
                yield wrapped.byte_vector(wrapped.field(root, 5))
        except (IndexError, struct.error):
            pass
    yield from (item for sub in entries(buffer, table) for item in scene_entries(buffer, sub))


def read_scene_object(data: bytes) -> tuple[str, tuple[float, float, float]] | None:
    try:
        buffer = FlatBuffer(data)
        table = buffer.root()
        name = buffer.string(buffer.field(table, 0))
        transform_field = buffer.field(table, 1)
        if not name or transform_field is None:
            return None
        transform = buffer.indirect(transform_field)
        translation = buffer.field(transform, 2)
        if translation is None:
            return None
        return name, struct.unpack_from("<3f", data, translation)
    except (IndexError, struct.error, UnicodeError):
        return None


def classify(name: str) -> str | None:
    lowered = name.lower()
    if lowered == "t1_b_pokemon_center_out":
        return "center"
    if len(lowered) == len("t1_b_cafe_a") and lowered.startswith("t1_b_cafe_"):
        return "cafe"
    if lowered.startswith("t1_b_boutique") and not lowered.endswith(("_light", "_col")):
        return "shop"
    if "hairsalon" in lowered or "hair_salon" in lowered:
        return "salon"
    if "taxi" in lowered:
        return "taxi"
    if "wildzone" in lowered and "entrance" in lowered:
        return "wild"
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    locations: dict[tuple[str, int, int], tuple[str, float, float]] = {}
    for path in args.input.rglob("*.bin"):
        try:
            buffer = FlatBuffer(path.read_bytes())
            root = buffer.root()
            start, count = buffer.vector(buffer.field(root, 4))
            roots = (buffer.indirect(start + index * 4) for index in range(count))
            for blob in (blob for entry in roots for blob in scene_entries(buffer, entry)):
                parsed = read_scene_object(blob)
                if parsed is None:
                    continue
                name, position = parsed
                kind = classify(name)
                if kind is None:
                    continue
                x, _, z = position
                if not (-100_000 < x < 100_000 and -100_000 < z < 100_000):
                    continue
                key = kind, round(x), round(z)
                locations[key] = name, x, z
        except (IndexError, struct.error):
            continue
    shops = [(x, z) for (kind, _, _), (_, x, z) in locations.items() if kind == "shop"]
    remaining = set(range(len(shops)))
    shop_clusters: list[tuple[float, float]] = []
    while remaining:
        cluster = {remaining.pop()}
        changed = True
        while changed:
            changed = False
            for index in list(remaining):
                if any(
                    (shops[index][0] - shops[member][0]) ** 2 +
                    (shops[index][1] - shops[member][1]) ** 2 <= 40 ** 2
                    for member in cluster
                ):
                    cluster.add(index)
                    remaining.remove(index)
                    changed = True
        if len(cluster) >= 5:
            shop_clusters.append((
                sum(shops[index][0] for index in cluster) / len(cluster),
                sum(shops[index][1] for index in cluster) / len(cluster),
            ))
    rows = [
        f"{kind}\t{x:.3f}\t{z:.3f}\t{name}"
        for (kind, _, _), (name, x, z) in locations.items()
        if kind != "shop" and not (kind == "salon" and abs(x) < 20 and abs(z) < 20)
    ]
    rows.extend(
        f"shop\t{x:.3f}\t{z:.3f}\tLumiose boutique"
        for x, z in shop_clusters
    )
    rows.sort()
    text = "\n".join(rows) + ("\n" if rows else "")
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    print(f"Found {len(rows)} native map locations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
