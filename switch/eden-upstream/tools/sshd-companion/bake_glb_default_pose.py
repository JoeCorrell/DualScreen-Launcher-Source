#!/usr/bin/env python3
"""Bake one sampled glTF animation frame into each target node's default TRS."""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
from pathlib import Path


JSON_CHUNK = 0x4E4F534A


def read_glb(path: Path) -> tuple[dict, list[tuple[int, bytes]]]:
    payload = path.read_bytes()
    if len(payload) < 12:
        raise ValueError(f"{path}: truncated GLB")
    magic, version, total = struct.unpack_from("<III", payload)
    if magic != 0x46546C67 or version != 2 or total != len(payload):
        raise ValueError(f"{path}: invalid GLB header")
    chunks: list[tuple[int, bytes]] = []
    offset = 12
    while offset < len(payload):
        size, kind = struct.unpack_from("<II", payload, offset)
        offset += 8
        chunks.append((kind, payload[offset : offset + size]))
        offset += size
    document = json.loads(next(data for kind, data in chunks if kind == JSON_CHUNK))
    return document, chunks


def accessor_values(document: dict, chunks: list[tuple[int, bytes]], index: int) -> list[list[float]]:
    accessor = document["accessors"][index]
    if accessor.get("componentType") != 5126 or accessor.get("sparse") is not None:
        raise ValueError("pose baker supports only dense float animation accessors")
    components = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
    view = document["bufferViews"][accessor["bufferView"]]
    buffer_index = view.get("buffer", 0)
    binary_chunks = [data for kind, data in chunks if kind != JSON_CHUNK]
    if buffer_index >= len(binary_chunks):
        raise ValueError("animation accessor references a missing GLB buffer")
    data = binary_chunks[buffer_index]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", components * 4)
    values = []
    for item in range(accessor["count"]):
        values.append(list(struct.unpack_from(f"<{components}f", data, offset + item * stride)))
    return values


def bake(path: Path, animation_name: str, fraction: float) -> None:
    document, chunks = read_glb(path)
    animations = document.get("animations", [])
    if not animations:
        raise ValueError(f"{path}: no animations")
    animation = next(
        (item for item in animations if item.get("name", "").lower() == animation_name.lower()),
        animations[0],
    )
    changed = 0
    changed_nodes: set[int] = set()
    for channel in animation.get("channels", []):
        target = channel.get("target", {})
        path_name = target.get("path")
        node_index = target.get("node")
        if path_name not in {"translation", "rotation", "scale"} or node_index is None:
            continue
        sampler = animation["samplers"][channel["sampler"]]
        times = [value[0] for value in accessor_values(document, chunks, sampler["input"])]
        values = accessor_values(document, chunks, sampler["output"])
        if not times or len(times) != len(values):
            raise ValueError(f"{path}: malformed animation sampler")
        target_time = times[0] + (times[-1] - times[0]) * fraction
        sample = min(range(len(times)), key=lambda item: abs(times[item] - target_time))
        value = values[sample]
        if path_name == "rotation":
            length = math.sqrt(sum(component * component for component in value))
            if length > 0:
                value = [component / length for component in value]
        document["nodes"][node_index][path_name] = value
        changed += 1
        changed_nodes.add(node_index)
    if changed == 0:
        raise ValueError(f"{path}: animation has no node transform channels")

    asset = document.setdefault("asset", {"version": "2.0"})
    extras = asset.setdefault("extras", {})
    extras["sshdDefaultPose"] = {
        "animation": animation.get("name", animation_name),
        "fraction": fraction,
        "channels": changed,
        "nodes": len(changed_nodes),
    }
    replacement = json.dumps(document, separators=(",", ":")).encode("utf-8")
    replacement += b" " * ((4 - len(replacement) % 4) % 4)
    output_chunks = [
        (kind, replacement if kind == JSON_CHUNK else data) for kind, data in chunks
    ]
    total = 12 + sum(8 + len(data) for _, data in output_chunks)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("wb") as stream:
        stream.write(struct.pack("<III", 0x46546C67, 2, total))
        for kind, data in output_chunks:
            stream.write(struct.pack("<II", len(data), kind))
            stream.write(data)
    os.replace(temporary, path)
    print(f"{path}: baked {changed} channels across {len(changed_nodes)} nodes")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("paths", nargs="+", type=Path)
    parser.add_argument("--animation", default="waitS")
    parser.add_argument("--fraction", type=float, default=0.37)
    arguments = parser.parse_args()
    if not 0.0 <= arguments.fraction <= 1.0:
        parser.error("--fraction must be between 0 and 1")
    for path in arguments.paths:
        bake(path, arguments.animation, arguments.fraction)


if __name__ == "__main__":
    main()
