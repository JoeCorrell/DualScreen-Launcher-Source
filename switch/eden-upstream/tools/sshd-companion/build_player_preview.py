#!/usr/bin/env python3
"""Build the two SSHD Link outfit previews from the user's extracted Alink model.

Skyward Sword HD keeps its character geometry in Wii-era BRRES containers and
its HD textures in a separate XTX bundle. ABMatt decodes the BRRES geometry,
skeleton and weights to Collada, while this script deswizzles and decodes the
matching ASTC images from model.xtx. It then assembles the exact model sets selected by the game's
tunic type: al + al_face + al_head + al_hands for the green hero outfit, and
pl_body + al_face + pl_head + pl_hands for the opening Skyloft outfit. The
body uses the game's real waitS.rchaC idle from PLCommonAnime rather than a
bind/T-pose. The textured, skinned GLBs are consumed by the Android companion. No copyrighted
game data is stored in the repository.

Dependencies for BRRES input:
    py -m pip install git+https://github.com/Robert-N7/abmatt.git
    py -m pip install Pillow texture2ddecoder

Examples:
    py build_player_preview.py --brres model.brres --output player
    py build_player_preview.py --dae-directory exported --output player
"""

from __future__ import annotations

import argparse
import importlib.util
import io
import json
import math
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable


IDENTITY = (
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0,
)


@dataclass
class Primitive:
    name: str
    material: str
    positions: list[tuple[float, float, float]]
    normals: list[tuple[float, float, float]]
    texcoords: list[tuple[float, float]]
    joints: list[tuple[int, int, int, int]]
    weights: list[tuple[float, float, float, float]]


@dataclass
class Bone:
    name: str
    parent: int | None
    local_matrix: tuple[float, ...]
    world_matrix: tuple[float, ...]


@dataclass
class Skeleton:
    bones: list[Bone]
    indices: dict[str, int]


@dataclass
class AnimationClip:
    name: str
    times: list[tuple[float]]
    translations: dict[str, list[tuple[float, float, float]]]
    rotations: dict[str, list[tuple[float, float, float, float]]]
    scales: dict[str, list[tuple[float, float, float]]]


@dataclass(frozen=True)
class XtxImage:
    width: int
    height: int
    format: int
    mip_count: int
    mip_offsets: tuple[int, ...]
    block_height_log2: int
    data: bytes


@dataclass(frozen=True)
class PreviewTexture:
    name: str
    width: int
    height: int
    png: bytes
    has_transparency: bool


def multiply(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    return tuple(
        sum(a[row * 4 + k] * b[k * 4 + column] for k in range(4))
        for row in range(4)
        for column in range(4)
    )


def transpose(matrix: tuple[float, ...]) -> tuple[float, ...]:
    """Convert the row-major NW4R/Collada matrices to glTF column-major order."""
    return tuple(matrix[row * 4 + column] for column in range(4) for row in range(4))


def inverse(matrix: tuple[float, ...]) -> tuple[float, ...]:
    augmented = [
        [matrix[row * 4 + column] for column in range(4)]
        + [1.0 if row == column else 0.0 for column in range(4)]
        for row in range(4)
    ]
    for column in range(4):
        pivot = max(range(column, 4), key=lambda row: abs(augmented[row][column]))
        if abs(augmented[pivot][column]) < 1e-9:
            raise ValueError("singular skeleton matrix")
        augmented[column], augmented[pivot] = augmented[pivot], augmented[column]
        divisor = augmented[column][column]
        augmented[column] = [value / divisor for value in augmented[column]]
        for row in range(4):
            if row == column:
                continue
            factor = augmented[row][column]
            augmented[row] = [
                augmented[row][index] - factor * augmented[column][index] for index in range(8)
            ]
    return tuple(augmented[row][column] for row in range(4) for column in range(4, 8))


def matrix_to_trs(
    matrix: tuple[float, ...],
) -> tuple[
    tuple[float, float, float],
    tuple[float, float, float, float],
    tuple[float, float, float],
]:
    translation = (matrix[3], matrix[7], matrix[11])
    scales = [
        math.sqrt(sum(matrix[row * 4 + column] ** 2 for row in range(3)))
        for column in range(3)
    ]
    rotation = [
        [matrix[row * 4 + column] / max(scales[column], 1e-9) for column in range(3)]
        for row in range(3)
    ]
    determinant = (
        rotation[0][0] * (rotation[1][1] * rotation[2][2] - rotation[1][2] * rotation[2][1])
        - rotation[0][1] * (rotation[1][0] * rotation[2][2] - rotation[1][2] * rotation[2][0])
        + rotation[0][2] * (rotation[1][0] * rotation[2][1] - rotation[1][1] * rotation[2][0])
    )
    if determinant < 0.0:
        scales[0] = -scales[0]
        for row in range(3):
            rotation[row][0] = -rotation[row][0]
    trace = rotation[0][0] + rotation[1][1] + rotation[2][2]
    if trace > 0.0:
        s = math.sqrt(trace + 1.0) * 2.0
        quaternion = (
            (rotation[2][1] - rotation[1][2]) / s,
            (rotation[0][2] - rotation[2][0]) / s,
            (rotation[1][0] - rotation[0][1]) / s,
            0.25 * s,
        )
    elif rotation[0][0] > rotation[1][1] and rotation[0][0] > rotation[2][2]:
        s = math.sqrt(1.0 + rotation[0][0] - rotation[1][1] - rotation[2][2]) * 2.0
        quaternion = (
            0.25 * s,
            (rotation[0][1] + rotation[1][0]) / s,
            (rotation[0][2] + rotation[2][0]) / s,
            (rotation[2][1] - rotation[1][2]) / s,
        )
    elif rotation[1][1] > rotation[2][2]:
        s = math.sqrt(1.0 + rotation[1][1] - rotation[0][0] - rotation[2][2]) * 2.0
        quaternion = (
            (rotation[0][1] + rotation[1][0]) / s,
            0.25 * s,
            (rotation[1][2] + rotation[2][1]) / s,
            (rotation[0][2] - rotation[2][0]) / s,
        )
    else:
        s = math.sqrt(1.0 + rotation[2][2] - rotation[0][0] - rotation[1][1]) * 2.0
        quaternion = (
            (rotation[0][2] + rotation[2][0]) / s,
            (rotation[1][2] + rotation[2][1]) / s,
            0.25 * s,
            (rotation[1][0] - rotation[0][1]) / s,
        )
    length = math.sqrt(sum(value * value for value in quaternion))
    quaternion = tuple(value / length for value in quaternion)
    return translation, quaternion, tuple(scales)


def euler_xyz_quaternion(x: float, y: float, z: float) -> tuple[float, float, float, float]:
    """NW4R MTX34RotXYZDeg expressed as a glTF quaternion (Rz * Ry * Rx)."""
    x, y, z = (math.radians(value) * 0.5 for value in (x, y, z))
    sx, cx = math.sin(x), math.cos(x)
    sy, cy = math.sin(y), math.cos(y)
    sz, cz = math.sin(z), math.cos(z)
    return (
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    )


def transform_position(matrix: tuple[float, ...], value: tuple[float, ...]) -> tuple[float, float, float]:
    x, y, z = value[:3]
    return (
        matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
        matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
        matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11],
    )


def transform_normal(matrix: tuple[float, ...], value: tuple[float, ...]) -> tuple[float, float, float]:
    x, y, z = value[:3]
    result = (
        matrix[0] * x + matrix[1] * y + matrix[2] * z,
        matrix[4] * x + matrix[5] * y + matrix[6] * z,
        matrix[8] * x + matrix[9] * y + matrix[10] * z,
    )
    length = math.sqrt(sum(component * component for component in result))
    return tuple(component / length for component in result) if length > 0.000001 else (0.0, 1.0, 0.0)


def namespace(root: ET.Element) -> str:
    return root.tag.split("}", 1)[0].removeprefix("{") if "}" in root.tag else ""


def qname(ns: str, name: str) -> str:
    return f"{{{ns}}}{name}" if ns else name


ASTC_BLOCKS = (
    (4, 4), (5, 4), (5, 5), (6, 5), (6, 6), (8, 5), (8, 6),
    (8, 8), (10, 5), (10, 6), (10, 8), (10, 10), (12, 10), (12, 12),
)


def astc_block_size(format_value: int) -> tuple[int, int]:
    if 0x79 <= format_value <= 0x86:
        return ASTC_BLOCKS[format_value - 0x79]
    if 0x87 <= format_value <= 0x94:
        return ASTC_BLOCKS[format_value - 0x87]
    raise ValueError(f"unsupported XTX texture format 0x{format_value:x}; Alink should use ASTC")


def round_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def block_linear_address(x: int, y: int, width_in_blocks: int, block_height: int) -> int:
    """Return the Tegra X1 block-linear address for a 16-byte ASTC block."""
    bytes_per_block = 16
    image_width_in_gobs = (width_in_blocks * bytes_per_block + 63) // 64
    gob = (
        (y // (8 * block_height)) * 512 * block_height * image_width_in_gobs
        + (x * bytes_per_block // 64) * 512 * block_height
        + (y % (8 * block_height) // 8) * 512
    )
    byte_x = x * bytes_per_block
    return (
        gob
        + ((byte_x % 64) // 32) * 256
        + ((y % 8) // 2) * 64
        + ((byte_x % 32) // 16) * 32
        + (y % 2) * 16
        + (byte_x % 16)
    )


def deswizzle_astc(image: XtxImage) -> tuple[bytes, int, int]:
    block_width, block_height = astc_block_size(image.format)
    width_in_blocks = (image.width + block_width - 1) // block_width
    height_in_blocks = (image.height + block_height - 1) // block_height
    gob_height = 1 << image.block_height_log2
    pitch = round_up(width_in_blocks * 16, 64)
    surface_size = pitch * round_up(height_in_blocks, gob_height * 8)
    if len(image.data) < surface_size:
        raise ValueError(
            f"XTX {image.width}x{image.height} mip 0 is truncated "
            f"({len(image.data)} < {surface_size})"
        )
    linear = bytearray(width_in_blocks * height_in_blocks * 16)
    for y in range(height_in_blocks):
        for x in range(width_in_blocks):
            source = block_linear_address(x, y, width_in_blocks, gob_height)
            destination = (y * width_in_blocks + x) * 16
            linear[destination:destination + 16] = image.data[source:source + 16]
    return bytes(linear), block_width, block_height


def read_xtx(path: Path) -> list[XtxImage]:
    data = path.read_bytes()
    if len(data) < 16 or data[:4] != b"DFvN":
        raise ValueError(f"{path.name} is not an NvFD XTX container")
    header_size, major_version, _minor_version = struct.unpack_from("<3I", data, 4)
    if major_version != 1:
        raise ValueError(f"unsupported XTX major version {major_version}")

    headers: list[tuple[int, int, int, int, tuple[int, ...], int]] = []
    payloads: list[bytes] = []
    position = header_size
    while position + 36 <= len(data):
        block_start = position
        magic, _size, data_size, data_offset, block_type, _identifier, _type_index = (
            struct.unpack_from("<2I2Q3I", data, position)
        )
        if magic != 0x4E764248:  # NvBH
            raise ValueError(f"invalid XTX block at 0x{position:x}")
        payload_start = block_start + data_offset
        payload_end = payload_start + data_size
        if payload_end > len(data):
            raise ValueError(f"truncated XTX block at 0x{position:x}")
        if block_type == 2:
            if data_size < 120:
                raise ValueError("truncated XTX texture header")
            (
                _image_size, _alignment, width, height, depth, target,
                format_value, mip_count, _slice_size,
            ) = struct.unpack_from("<Q8I", data, payload_start)
            mip_offsets = struct.unpack_from("<17I", data, payload_start + 40)
            texture_layout = struct.unpack_from("<I", data, payload_start + 108)[0]
            if depth != 1 or target != 1:
                raise ValueError(f"unsupported XTX target/depth {target}/{depth}")
            if not 1 <= mip_count <= 17:
                raise ValueError(f"invalid XTX mip count {mip_count}")
            astc_block_size(format_value)
            headers.append(
                (width, height, format_value, mip_count, tuple(mip_offsets[:mip_count]), texture_layout & 7)
            )
        elif block_type == 3:
            payloads.append(data[payload_start:payload_end])
        position = payload_end

    if not headers or len(headers) != len(payloads):
        raise ValueError(
            f"XTX texture header/data mismatch ({len(headers)} headers, {len(payloads)} payloads)"
        )
    return [
        XtxImage(width, height, format_value, mip_count, offsets, layout, payload)
        for (width, height, format_value, mip_count, offsets, layout), payload
        in zip(headers, payloads, strict=True)
    ]


def external_texture_order(brres_path: Path) -> list[str]:
    if importlib.util.find_spec("abmatt") is None:
        raise RuntimeError("ABMatt is required to match the XTX images to their BRRES texture names")
    from abmatt.brres.brres import Brres

    archive = Brres(str(brres_path))
    # SSHD replaces the one-pixel BRRES texture stubs, in archive order, with
    # the same number of HD images from model.xtx. Real embedded BRRES images
    # keep their original dimensions and therefore do not consume an XTX slot.
    return [
        texture.name for texture in archive.textures
        if texture.width == 1 and texture.height == 1
    ]


def material_texture_map(path: Path) -> dict[str, str]:
    """Read ABMatt's exact material -> diffuse texture binding from Collada."""
    root = ET.parse(path).getroot()
    ns = namespace(root)
    images = {
        image.get("id", ""): image.get("name", "")
        for image in root.findall(f".//{qname(ns, 'library_images')}/{qname(ns, 'image')}")
    }
    effects: dict[str, str] = {}
    for effect in root.findall(f".//{qname(ns, 'library_effects')}/{qname(ns, 'effect')}"):
        diffuse = effect.find(
            f".//{qname(ns, 'technique')}/{qname(ns, 'phong')}/"
            f"{qname(ns, 'diffuse')}/{qname(ns, 'texture')}"
        )
        if diffuse is None:
            continue
        sampler_id = diffuse.get("texture", "")
        sampler = effect.find(f"./{qname(ns, 'profile_COMMON')}/{qname(ns, 'newparam')}[@sid='{sampler_id}']")
        source = sampler.find(f"./{qname(ns, 'sampler2D')}/{qname(ns, 'source')}") if sampler is not None else None
        surface_id = source.text if source is not None else ""
        surface = effect.find(f"./{qname(ns, 'profile_COMMON')}/{qname(ns, 'newparam')}[@sid='{surface_id}']")
        image_ref = surface.find(f"./{qname(ns, 'surface')}/{qname(ns, 'init_from')}") if surface is not None else None
        image_name = images.get(image_ref.text if image_ref is not None else "", "")
        if image_name:
            effects[effect.get("id", "")] = image_name

    result: dict[str, str] = {}
    for material in root.findall(f".//{qname(ns, 'library_materials')}/{qname(ns, 'material')}"):
        instance = material.find(qname(ns, "instance_effect"))
        effect_id = instance.get("url", "").removeprefix("#") if instance is not None else ""
        texture_name = effects.get(effect_id)
        material_name = material.get("id") or material.get("name")
        if material_name and texture_name:
            result[material_name] = texture_name
    return result


def decode_preview_textures(
    xtx_path: Path,
    brres_path: Path,
    required_names: set[str],
) -> dict[str, PreviewTexture]:
    if importlib.util.find_spec("PIL") is None or importlib.util.find_spec("texture2ddecoder") is None:
        raise RuntimeError("Pillow and texture2ddecoder are required to decode Alink's ASTC textures")
    from PIL import Image
    import texture2ddecoder

    images = read_xtx(xtx_path)
    names = external_texture_order(brres_path)
    if len(names) != len(images):
        raise ValueError(
            f"Alink BRRES/XTX texture count mismatch ({len(names)} names, {len(images)} images)"
        )
    missing = required_names - set(names)
    if missing:
        raise ValueError("XTX is missing required Link textures: " + ", ".join(sorted(missing)))

    result: dict[str, PreviewTexture] = {}
    for name, xtx_image in zip(names, images, strict=True):
        if name not in required_names:
            continue
        linear, block_width, block_height = deswizzle_astc(xtx_image)
        bgra = texture2ddecoder.decode_astc(
            linear, xtx_image.width, xtx_image.height, block_width, block_height
        )
        image = Image.frombytes(
            "RGBA", (xtx_image.width, xtx_image.height), bgra, "raw", "BGRA"
        )
        alpha_minimum = image.getchannel("A").getextrema()[0]
        encoded = io.BytesIO()
        image.save(encoded, format="PNG", optimize=True)
        result[name] = PreviewTexture(
            name, xtx_image.width, xtx_image.height, encoded.getvalue(), alpha_minimum < 250
        )
    return result


def bone_world_matrices(path: Path) -> dict[str, tuple[float, ...]]:
    root = ET.parse(path).getroot()
    ns = namespace(root)
    scene = root.find(f".//{qname(ns, 'visual_scene')}")
    if scene is None:
        raise ValueError(f"{path.name} has no visual scene")
    result: dict[str, tuple[float, ...]] = {}

    def visit(node: ET.Element, parent: tuple[float, ...]) -> None:
        matrix_node = node.find(qname(ns, "matrix"))
        local = IDENTITY
        if matrix_node is not None and matrix_node.text:
            values = tuple(float(value) for value in matrix_node.text.split())
            if len(values) == 16:
                local = values
        world = multiply(parent, local)
        identifier = node.get("id") or node.get("name")
        if identifier:
            result[identifier] = world
        for child in node.findall(qname(ns, "node")):
            visit(child, world)

    for child in scene.findall(qname(ns, "node")):
        visit(child, IDENTITY)
    return result


def read_skeleton(path: Path) -> Skeleton:
    root = ET.parse(path).getroot()
    ns = namespace(root)
    scene = root.find(f".//{qname(ns, 'visual_scene')}")
    if scene is None:
        raise ValueError(f"{path.name} has no visual scene")
    bones: list[Bone] = []
    indices: dict[str, int] = {}

    def visit(node: ET.Element, parent: int | None, parent_world: tuple[float, ...]) -> None:
        current_parent = parent
        current_world = parent_world
        if node.get("type") == "JOINT":
            name = node.get("sid") or node.get("id") or node.get("name")
            if not name:
                raise ValueError(f"unnamed joint in {path.name}")
            matrix_node = node.find(qname(ns, "matrix"))
            local = IDENTITY
            if matrix_node is not None and matrix_node.text:
                local = tuple(float(value) for value in matrix_node.text.split())
            if len(local) != 16:
                raise ValueError(f"invalid joint matrix for {name} in {path.name}")
            current_world = multiply(parent_world, local)
            current_parent = len(bones)
            indices[name] = current_parent
            bones.append(Bone(name, parent, local, current_world))
        for child in node.findall(qname(ns, "node")):
            visit(child, current_parent, current_world)

    for child in scene.findall(qname(ns, "node")):
        visit(child, None, IDENTITY)
    if not bones:
        raise ValueError(f"{path.name} contains no Collada joints")
    return Skeleton(bones, indices)


def read_sources(mesh: ET.Element, ns: str) -> dict[str, list[tuple[float, ...]]]:
    sources: dict[str, list[tuple[float, ...]]] = {}
    for source in mesh.findall(qname(ns, "source")):
        identifier = source.get("id")
        array = source.find(qname(ns, "float_array"))
        accessor = source.find(f"./{qname(ns, 'technique_common')}/{qname(ns, 'accessor')}")
        if not identifier or array is None or not array.text or accessor is None:
            continue
        stride = int(accessor.get("stride", "1"))
        values = [float(value) for value in array.text.split()]
        sources[identifier] = [tuple(values[index:index + stride]) for index in range(0, len(values), stride)]
    return sources


def controller_vertex_weights(
    root: ET.Element, ns: str, skeleton: Skeleton
) -> dict[str, list[tuple[tuple[int, int, int, int], tuple[float, float, float, float]]]]:
    result: dict[str, list[tuple[tuple[int, int, int, int], tuple[float, float, float, float]]]] = {}
    for controller in root.findall(f".//{qname(ns, 'library_controllers')}/{qname(ns, 'controller')}"):
        skin = controller.find(qname(ns, "skin"))
        if skin is None:
            continue
        geometry_id = skin.get("source", "").removeprefix("#")
        names: dict[str, list[str]] = {}
        floats: dict[str, list[float]] = {}
        for source in skin.findall(qname(ns, "source")):
            identifier = source.get("id", "")
            name_array = source.find(qname(ns, "Name_array"))
            float_array = source.find(qname(ns, "float_array"))
            if name_array is not None and name_array.text:
                names[identifier] = name_array.text.split()
            if float_array is not None and float_array.text:
                floats[identifier] = [float(value) for value in float_array.text.split()]
        vertex_weights = skin.find(qname(ns, "vertex_weights"))
        if vertex_weights is None:
            continue
        inputs = {
            item.get("semantic", ""): (
                item.get("source", "").removeprefix("#"), int(item.get("offset", "0"))
            )
            for item in vertex_weights.findall(qname(ns, "input"))
        }
        if "JOINT" not in inputs or "WEIGHT" not in inputs:
            continue
        joint_source, joint_offset = inputs["JOINT"]
        weight_source, weight_offset = inputs["WEIGHT"]
        joint_names = names[joint_source]
        weight_values = floats[weight_source]
        stride = max(offset for _source, offset in inputs.values()) + 1
        vcount_node = vertex_weights.find(qname(ns, "vcount"))
        values_node = vertex_weights.find(qname(ns, "v"))
        if vcount_node is None or values_node is None or not vcount_node.text or not values_node.text:
            continue
        counts = [int(value) for value in vcount_node.text.split()]
        values = [int(value) for value in values_node.text.split()]
        cursor = 0
        decoded = []
        for count in counts:
            influences = []
            for _ in range(count):
                joint_local = values[cursor + joint_offset]
                weight_index = values[cursor + weight_offset]
                cursor += stride
                joint_name = joint_names[joint_local]
                if joint_name not in skeleton.indices:
                    raise ValueError(f"controller references unknown joint {joint_name}")
                influences.append((skeleton.indices[joint_name], weight_values[weight_index]))
            influences.sort(key=lambda value: value[1], reverse=True)
            influences = influences[:4]
            total = sum(value[1] for value in influences)
            if total <= 1e-8:
                influences = [(0, 1.0)]
                total = 1.0
            joints = tuple([value[0] for value in influences] + [0] * (4 - len(influences)))
            weights = tuple([value[1] / total for value in influences] + [0.0] * (4 - len(influences)))
            decoded.append((joints, weights))
        result[geometry_id] = decoded
    return result


def read_dae(
    path: Path,
    matrix: tuple[float, ...] = IDENTITY,
    material_filter: Callable[[str], bool] = lambda _material: True,
    skeleton: Skeleton | None = None,
    rigid_joint: int | None = None,
) -> list[Primitive]:
    root = ET.parse(path).getroot()
    ns = namespace(root)
    # Head, face and hand archives own separate local skeletons. They are
    # intentionally attached rigidly to the matching body joint, so their
    # unrelated controller joint names must not be resolved against the body.
    controller_weights = (
        controller_vertex_weights(root, ns, skeleton)
        if skeleton is not None and rigid_joint is None else {}
    )
    output: list[Primitive] = []
    for geometry in root.findall(f".//{qname(ns, 'library_geometries')}/{qname(ns, 'geometry')}"):
        mesh = geometry.find(qname(ns, "mesh"))
        if mesh is None:
            continue
        sources = read_sources(mesh, ns)
        geometry_weights = controller_weights.get(geometry.get("id", ""))
        vertices: dict[str, dict[str, str]] = {}
        for vertex_group in mesh.findall(qname(ns, "vertices")):
            identifier = vertex_group.get("id")
            if not identifier:
                continue
            vertices[identifier] = {
                item.get("semantic", ""): item.get("source", "").removeprefix("#")
                for item in vertex_group.findall(qname(ns, "input"))
            }
        for triangle_index, triangles in enumerate(mesh.findall(qname(ns, "triangles"))):
            material = triangles.get("material", "default")
            if not material_filter(material):
                continue
            inputs = []
            for item in triangles.findall(qname(ns, "input")):
                semantic = item.get("semantic", "")
                source = item.get("source", "").removeprefix("#")
                offset = int(item.get("offset", "0"))
                if semantic == "VERTEX":
                    source = vertices.get(source, {}).get("POSITION", source)
                    semantic = "POSITION"
                inputs.append((semantic, source, offset))
            stride = max((item[2] for item in inputs), default=-1) + 1
            index_node = triangles.find(qname(ns, "p"))
            if stride <= 0 or index_node is None or not index_node.text:
                continue
            raw_indices = [int(value) for value in index_node.text.split()]
            positions: list[tuple[float, float, float]] = []
            normals: list[tuple[float, float, float]] = []
            texcoords: list[tuple[float, float]] = []
            joints: list[tuple[int, int, int, int]] = []
            weights: list[tuple[float, float, float, float]] = []
            for corner in range(len(raw_indices) // stride):
                values = {
                    semantic: sources[source][raw_indices[corner * stride + offset]]
                    for semantic, source, offset in inputs
                    if source in sources and raw_indices[corner * stride + offset] < len(sources[source])
                }
                position = values.get("POSITION")
                if position is None:
                    continue
                positions.append(transform_position(matrix, position))
                position_input = next(
                    (item for item in inputs if item[0] == "POSITION"), None
                )
                position_index = (
                    raw_indices[corner * stride + position_input[2]] if position_input is not None else 0
                )
                if rigid_joint is not None:
                    joints.append((rigid_joint, 0, 0, 0))
                    weights.append((1.0, 0.0, 0.0, 0.0))
                elif geometry_weights is not None and position_index < len(geometry_weights):
                    joint_value, weight_value = geometry_weights[position_index]
                    joints.append(joint_value)
                    weights.append(weight_value)
                normal = values.get("NORMAL")
                if normal is not None:
                    normals.append(transform_normal(matrix, normal))
                texcoord = values.get("TEXCOORD")
                if texcoord is not None:
                    texcoords.append((texcoord[0], texcoord[1]))
            if positions:
                output.append(
                    Primitive(
                        name=f"{path.stem}_{geometry.get('name', geometry.get('id', 'mesh'))}_{triangle_index}",
                        material=material,
                        positions=positions,
                        normals=normals if len(normals) == len(positions) else [],
                        texcoords=texcoords if len(texcoords) == len(positions) else [],
                        joints=joints if len(joints) == len(positions) else [],
                        weights=weights if len(weights) == len(positions) else [],
                    )
                )
    return output


def read_resdic(data: bytes, offset: int) -> list[tuple[str, int]]:
    _length, count = struct.unpack_from(">II", data, offset)
    result = []
    for index in range(1, count + 1):
        entry = offset + 8 + index * 16
        name_offset, data_offset = struct.unpack_from(">II", data, entry + 8)
        name_start = offset + name_offset
        name_end = data.index(0, name_start)
        result.append((data[name_start:name_end].decode("ascii"), offset + data_offset))
    return result


def lz11_decompress(data: bytes) -> bytes:
    if len(data) < 4 or data[0] != 0x11:
        raise ValueError("external PLCommonAnime resource is not Nintendo LZ11")
    output_size = int.from_bytes(data[1:4], "little")
    cursor = 4
    if output_size == 0:
        output_size = int.from_bytes(data[4:8], "little")
        cursor = 8
    output = bytearray()
    while len(output) < output_size:
        flags = data[cursor]
        cursor += 1
        for bit in range(8):
            if len(output) >= output_size:
                break
            if not flags & (0x80 >> bit):
                output.append(data[cursor])
                cursor += 1
                continue
            first, second = data[cursor:cursor + 2]
            cursor += 2
            high = first >> 4
            if high == 0:
                third = data[cursor]
                cursor += 1
                length = ((first & 0xF) << 4 | second >> 4) + 0x11
                distance = ((second & 0xF) << 8 | third) + 1
            elif high == 1:
                third, fourth = data[cursor:cursor + 2]
                cursor += 2
                length = ((first & 0xF) << 12 | second << 4 | third >> 4) + 0x111
                distance = ((third & 0xF) << 8 | fourth) + 1
            else:
                length = high + 1
                distance = ((first & 0xF) << 8 | second) + 1
            if distance > len(output):
                raise ValueError("invalid LZ11 back-reference")
            for _ in range(length):
                output.append(output[-distance])
    return bytes(output[:output_size])


def external_brres_resource(brres_path: Path, name: str) -> bytes:
    data = brres_path.read_bytes()
    if data[:4] != b"bres":
        raise ValueError(f"{brres_path.name} is not a BRRES")
    top = dict(read_resdic(data, 0x18))
    if "External" not in top:
        raise ValueError(f"{brres_path.name} has no External resource folder")
    entries = read_resdic(data, top["External"])
    offsets = sorted(offset for _entry_name, offset in entries)
    entry_map = dict(entries)
    if name not in entry_map:
        raise ValueError(f"PLCommonAnime is missing {name}")
    start = entry_map[name]
    end = next((offset for offset in offsets if offset > start), len(data))
    return lz11_decompress(data[start:end])


def brres_subfile(data: bytes, category: str, name: str) -> tuple[int, bytes]:
    top = dict(read_resdic(data, 0x18))
    if category not in top:
        raise ValueError(f"animation BRRES is missing {category}")
    entries = dict(read_resdic(data, top[category]))
    if name not in entries:
        raise ValueError(f"animation BRRES is missing {name}")
    return entries[name], data


def signed(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def hermite(v0: float, t0: float, v1: float, t1: float, s: float, distance: float) -> float:
    s_1 = s - 1.0
    return v0 + s * (s * ((2.0 * s - 3.0) * (v0 - v1))) + distance * s_1 * (s_1 * t0 + s * t1)


def sample_fvs(data: bytes, offset: int, format_value: int, frame: float) -> float:
    count = struct.unpack_from(">H", data, offset)[0]
    if count <= 0:
        raise ValueError("CHR0 keyframe list is empty")
    keys: list[tuple[float, float, float]] = []
    if format_value == 1:
        scale, base = struct.unpack_from(">ff", data, offset + 8)
        for index in range(count):
            packed = struct.unpack_from(">I", data, offset + 16 + index * 4)[0]
            keys.append(
                (
                    float(packed >> 24),
                    scale * ((packed >> 12) & 0xFFF) + base,
                    signed(packed & 0xFFF, 12) / 32.0,
                )
            )
    elif format_value == 2:
        scale, base = struct.unpack_from(">ff", data, offset + 8)
        for index in range(count):
            raw_frame, value, slope = struct.unpack_from(">hHh", data, offset + 16 + index * 6)
            keys.append((raw_frame / 32.0, scale * value + base, slope / 256.0))
    elif format_value == 3:
        keys = [
            struct.unpack_from(">fff", data, offset + 8 + index * 12) for index in range(count)
        ]
    else:
        raise ValueError(f"unsupported CHR0 FVS format {format_value}")
    if frame <= keys[0][0]:
        return keys[0][1]
    if frame >= keys[-1][0]:
        return keys[-1][1]
    for left, right in zip(keys, keys[1:]):
        if frame <= right[0]:
            distance = frame - left[0]
            span = right[0] - left[0]
            return hermite(left[1], left[2], right[1], right[2], distance / span, distance)
    return keys[-1][1]


def sample_cv(data: bytes, offset: int, format_value: int, num_frames: int, frame: float) -> float:
    frame = max(0.0, min(float(num_frames), frame))
    left = int(frame)
    right = min(num_frames, left + 1)
    blend = frame - left

    def value(index: int) -> float:
        if format_value == 4:
            scale, base = struct.unpack_from(">ff", data, offset)
            return data[offset + 8 + index] * scale + base
        if format_value == 5:
            scale, base = struct.unpack_from(">ff", data, offset)
            return struct.unpack_from(">H", data, offset + 8 + index * 2)[0] * scale + base
        if format_value == 6:
            return struct.unpack_from(">f", data, offset + index * 4)[0]
        raise ValueError(f"unsupported CHR0 CV format {format_value}")

    value_left = value(left)
    return value_left + (value(right) - value_left) * blend


def decode_chr0(data: bytes, offset: int, skeleton: Skeleton, name: str) -> AnimationClip:
    if data[offset:offset + 4] != b"CHR0":
        raise ValueError(f"{name} did not resolve to CHR0")
    revision = struct.unpack_from(">I", data, offset + 8)[0]
    if revision != 5:
        raise ValueError(f"unsupported CHR0 revision {revision}")
    dictionary_offset = struct.unpack_from(">I", data, offset + 0x10)[0]
    num_frames, num_nodes = struct.unpack_from(">HH", data, offset + 0x20)
    records = read_resdic(data, offset + dictionary_offset)
    if len(records) != num_nodes:
        raise ValueError(f"CHR0 node count mismatch ({len(records)} != {num_nodes})")
    record_map = {record_name: record_offset for record_name, record_offset in records}
    frames = [float(frame) for frame in range(num_frames + 1)]
    times = [(frame / 30.0,) for frame in frames]
    translations: dict[str, list[tuple[float, float, float]]] = {}
    rotations: dict[str, list[tuple[float, float, float, float]]] = {}
    scales: dict[str, list[tuple[float, float, float]]] = {}

    def components(
        record: int,
        flags: int,
        cursor: int,
        count: int,
        format_value: int,
        constant_bits: tuple[int, ...],
        sample_frame: float,
    ) -> tuple[tuple[float, ...], int]:
        result = []
        for index in range(count):
            raw = struct.unpack_from(">I", data, cursor)[0]
            is_constant = format_value == 0 or bool(flags & constant_bits[index])
            if is_constant:
                result.append(struct.unpack_from(">f", data, cursor)[0])
            else:
                animation_offset = record + signed(raw, 32)
                if format_value <= 3:
                    result.append(sample_fvs(data, animation_offset, format_value, sample_frame))
                else:
                    result.append(sample_cv(data, animation_offset, format_value, num_frames, sample_frame))
            cursor += 4
        return tuple(result), cursor

    for bone in skeleton.bones:
        record = record_map.get(bone.name)
        if record is None:
            continue
        flags = struct.unpack_from(">I", data, record + 4)[0]
        bone_translations = []
        bone_rotations = []
        bone_scales = []
        bind_translation, bind_rotation, bind_scale = matrix_to_trs(bone.local_matrix)
        for frame in frames:
            cursor = record + 8
            scale = bind_scale
            rotation = bind_rotation
            translation = bind_translation
            if flags & (1 << 22):
                scale_count = 1 if flags & (1 << 4) else 3
                values, cursor = components(
                    record, flags, cursor, scale_count, (flags >> 25) & 3,
                    (1 << 13, 1 << 14, 1 << 15), frame,
                )
                scale = (values[0],) * 3 if scale_count == 1 else values
            elif flags & (1 << 3):
                scale = (1.0, 1.0, 1.0)
            if flags & (1 << 23):
                values, cursor = components(
                    record, flags, cursor, 3, (flags >> 27) & 7,
                    (1 << 16, 1 << 17, 1 << 18), frame,
                )
                rotation = euler_xyz_quaternion(*values)
            elif flags & (1 << 5):
                rotation = (0.0, 0.0, 0.0, 1.0)
            if flags & (1 << 24):
                values, cursor = components(
                    record, flags, cursor, 3, (flags >> 30) & 3,
                    (1 << 19, 1 << 20, 1 << 21), frame,
                )
                translation = values
            elif flags & (1 << 6):
                translation = (0.0, 0.0, 0.0)
            bone_scales.append(scale)
            bone_rotations.append(rotation)
            bone_translations.append(translation)
        scales[bone.name] = bone_scales
        rotations[bone.name] = bone_rotations
        translations[bone.name] = bone_translations
    return AnimationClip(name, times, translations, rotations, scales)


def load_wait_animation(animation_brres: Path, skeleton: Skeleton) -> AnimationClip:
    wait_brres = external_brres_resource(animation_brres, "waitS.rchaC")
    offset, data = brres_subfile(wait_brres, "AnmChr(NW4R)", "waitS")
    return decode_chr0(data, offset, skeleton, "waitS")


def build_glb(
    primitives: Iterable[Primitive],
    material_textures: dict[str, str],
    preview_textures: dict[str, PreviewTexture],
    skeleton: Skeleton,
    animation: AnimationClip,
    output: Path,
) -> None:
    binary = bytearray()
    buffer_views: list[dict] = []
    accessors: list[dict] = []
    materials: list[dict] = []
    material_indices: dict[str, int] = {}
    gltf_primitives: list[dict] = []
    images: list[dict] = []
    textures: list[dict] = []
    texture_indices: dict[str, int] = {}

    def append_view(payload: bytes, target: int | None = 34962) -> int:
        while len(binary) % 4:
            binary.append(0)
        offset = len(binary)
        binary.extend(payload)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(payload)}
        if target is not None:
            view["target"] = target
        buffer_views.append(view)
        return len(buffer_views) - 1

    def append_accessor(
        values: list[tuple[float, ...]],
        kind: str,
        bounds: bool = False,
        component_type: int = 5126,
    ) -> int:
        flattened = [component for value in values for component in value]
        formats = {5126: "f", 5123: "H"}
        view = append_view(struct.pack(f"<{len(flattened)}{formats[component_type]}", *flattened))
        accessor = {
            "bufferView": view, "componentType": component_type, "count": len(values), "type": kind
        }
        if bounds:
            accessor["min"] = [min(value[index] for value in values) for index in range(len(values[0]))]
            accessor["max"] = [max(value[index] for value in values) for index in range(len(values[0]))]
        accessors.append(accessor)
        return len(accessors) - 1

    def texture_index(name: str) -> int:
        existing = texture_indices.get(name)
        if existing is not None:
            return existing
        texture = preview_textures.get(name)
        if texture is None:
            raise ValueError(f"material references undecoded game texture {name}")
        image_index = len(images)
        images.append(
            {
                "name": name,
                "mimeType": "image/png",
                "bufferView": append_view(texture.png, target=None),
            }
        )
        result = len(textures)
        textures.append({"name": name, "sampler": 0, "source": image_index})
        texture_indices[name] = result
        return result

    for primitive in primitives:
        if primitive.material not in material_indices:
            texture_name = material_textures.get(primitive.material)
            if texture_name is None:
                raise ValueError(f"no diffuse game texture is bound to material {primitive.material}")
            texture = preview_textures[texture_name]
            material = {
                "name": primitive.material,
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                    "baseColorTexture": {"index": texture_index(texture_name)},
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.78,
                },
                "doubleSided": True,
            }
            if texture.has_transparency:
                material["alphaMode"] = "MASK"
                material["alphaCutoff"] = 0.25
            material_indices[primitive.material] = len(materials)
            materials.append(material)
        attributes = {"POSITION": append_accessor(primitive.positions, "VEC3", bounds=True)}
        if primitive.normals:
            attributes["NORMAL"] = append_accessor(primitive.normals, "VEC3")
        if primitive.texcoords:
            attributes["TEXCOORD_0"] = append_accessor(primitive.texcoords, "VEC2")
        if not primitive.joints or not primitive.weights:
            raise ValueError(f"{primitive.name} has no skin weights")
        attributes["JOINTS_0"] = append_accessor(
            primitive.joints, "VEC4", component_type=5123
        )
        attributes["WEIGHTS_0"] = append_accessor(primitive.weights, "VEC4")
        gltf_primitives.append(
            {"attributes": attributes, "material": material_indices[primitive.material], "mode": 4}
        )

    # Start the asset in a sampled waitS frame as well as shipping the full
    # animation. Some glTF runtimes do not apply an animation until their first
    # render tick; baking the same real idle sample into node TRS prevents that
    # first (or stalled) frame from ever exposing the BRRES bind/T-pose.
    pose_sample = min(
        len(animation.times) - 1,
        max(0, round((len(animation.times) - 1) * 0.37)),
    )
    nodes = []
    for bone in skeleton.bones:
        translation, rotation, scale = matrix_to_trs(bone.local_matrix)
        animated_translations = animation.translations.get(bone.name)
        animated_rotations = animation.rotations.get(bone.name)
        animated_scales = animation.scales.get(bone.name)
        if animated_translations and pose_sample < len(animated_translations):
            translation = animated_translations[pose_sample]
        if animated_rotations and pose_sample < len(animated_rotations):
            rotation = animated_rotations[pose_sample]
        if animated_scales and pose_sample < len(animated_scales):
            scale = animated_scales[pose_sample]
        node = {
            "name": bone.name,
            "translation": list(translation),
            "rotation": list(rotation),
            "scale": list(scale),
        }
        children = [index for index, child in enumerate(skeleton.bones) if child.parent == len(nodes)]
        if children:
            node["children"] = children
        nodes.append(node)
    mesh_node = len(nodes)
    nodes.append({"name": "Link", "mesh": 0, "skin": 0})
    scene_nodes = [index for index, bone in enumerate(skeleton.bones) if bone.parent is None] + [mesh_node]
    inverse_binds = [transpose(inverse(bone.world_matrix)) for bone in skeleton.bones]
    inverse_bind_accessor = append_accessor(inverse_binds, "MAT4")

    animation_samplers = []
    animation_channels = []
    time_accessor = append_accessor(animation.times, "SCALAR", bounds=True)
    for bone_name, path, values in (
        *[(name, "translation", value) for name, value in animation.translations.items()],
        *[(name, "rotation", value) for name, value in animation.rotations.items()],
        *[(name, "scale", value) for name, value in animation.scales.items()],
    ):
        if bone_name not in skeleton.indices:
            continue
        sampler = len(animation_samplers)
        animation_samplers.append(
            {
                "input": time_accessor,
                "output": append_accessor(values, "VEC4" if path == "rotation" else "VEC3"),
                "interpolation": "LINEAR",
            }
        )
        animation_channels.append(
            {"sampler": sampler, "target": {"node": skeleton.indices[bone_name], "path": path}}
        )

    document = {
        "asset": {
            "version": "2.0",
            "generator": "Eden SSHD companion preview builder",
            "extras": {"sshdDefaultPose": {"animation": animation.name, "fraction": 0.37}},
        },
        "scene": 0,
        "scenes": [{"nodes": scene_nodes}],
        "nodes": nodes,
        "meshes": [{"name": "Link", "primitives": gltf_primitives}],
        "skins": [
            {
                "name": "LinkSkeleton",
                "joints": list(range(len(skeleton.bones))),
                "skeleton": scene_nodes[0],
                "inverseBindMatrices": inverse_bind_accessor,
            }
        ],
        "animations": [
            {"name": animation.name, "samplers": animation_samplers, "channels": animation_channels}
        ],
        "materials": materials,
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 10497, "wrapT": 10497}],
        "textures": textures,
        "images": images,
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    json_chunk = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((4 - len(json_chunk) % 4) % 4)
    binary += b"\0" * ((4 - len(binary) % 4) % 4)
    total = 12 + 8 + len(json_chunk) + 8 + len(binary)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(struct.pack("<III", 0x46546C67, 2, total))
        stream.write(struct.pack("<II", len(json_chunk), 0x4E4F534A))
        stream.write(json_chunk)
        stream.write(struct.pack("<II", len(binary), 0x004E4942))
        stream.write(binary)


def export_brres(brres: Path, destination: Path) -> None:
    if importlib.util.find_spec("abmatt") is None:
        raise RuntimeError("ABMatt is required for --brres; install it with the command in this script's header")
    destination.mkdir(parents=True, exist_ok=True)
    output = destination / "Link.dae"
    helper = (
        "import numpy as np,sys; "
        "np.float=float; "
        "sys.argv=['abmatt','convert',sys.argv[1],'to',sys.argv[2],'--overwrite']; "
        "from abmatt.__main__ import main; main()"
    )
    subprocess.run([sys.executable, "-c", helper, str(brres), str(output)], check=True)


def build_outfit(
    *,
    name: str,
    body: Path,
    face: Path,
    head: Path,
    hands: Path,
    brres_path: Path,
    animation_brres: Path,
    xtx_path: Path,
    output: Path,
) -> tuple[int, int, int]:
    required = (body, face, head, hands)
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise FileNotFoundError(f"missing ABMatt exports for {name}: " + ", ".join(missing))

    skeleton = read_skeleton(body)
    bones = {bone.name: bone.world_matrix for bone in skeleton.bones}
    for bone in ("head", "handL", "handR"):
        if bone not in bones:
            raise ValueError(f"{name} body is missing required bind bone {bone}")

    primitives = read_dae(body, skeleton=skeleton)
    primitives += read_dae(face, bones["head"], skeleton=skeleton, rigid_joint=skeleton.indices["head"])
    primitives += read_dae(head, bones["head"], skeleton=skeleton, rigid_joint=skeleton.indices["head"])
    # Both al_hands and pl_hands contain seven mutually-exclusive pose meshes
    # per side. A is the game's neutral/default hand for this static preview.
    primitives += read_dae(
        hands, bones["handL"], lambda material: material.lower() == "al_handla_m",
        skeleton=skeleton, rigid_joint=skeleton.indices["handL"],
    )
    primitives += read_dae(
        hands, bones["handR"], lambda material: material.lower() == "al_handra_m",
        skeleton=skeleton, rigid_joint=skeleton.indices["handR"],
    )
    if not primitives:
        raise ValueError(f"no {name} geometry was decoded")

    material_textures: dict[str, str] = {}
    for path in required:
        material_textures.update(material_texture_map(path))
    used_materials = {primitive.material for primitive in primitives}
    unbound_materials = used_materials - material_textures.keys()
    if unbound_materials:
        raise ValueError(
            f"{name} Collada materials have no diffuse game texture: "
            + ", ".join(sorted(unbound_materials))
        )
    texture_names = {material_textures[material] for material in used_materials}
    preview_textures = decode_preview_textures(xtx_path, brres_path, texture_names)
    animation = load_wait_animation(animation_brres, skeleton)
    build_glb(primitives, material_textures, preview_textures, skeleton, animation, output)
    return len(primitives), sum(len(primitive.positions) for primitive in primitives) // 3, len(preview_textures)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--brres", type=Path, help="Alink g3d/model.brres from the user's game")
    source.add_argument("--dae-directory", type=Path, help="directory containing ABMatt's Link-*.dae exports")
    parser.add_argument(
        "--xtx",
        type=Path,
        help="Alink g3d/model.xtx (defaults to model.xtx beside the BRRES/DAE exports)",
    )
    parser.add_argument(
        "--animation-brres",
        type=Path,
        help="PLCommonAnime g3d/model.brres containing waitS.rchaC",
    )
    parser.add_argument("--output", type=Path, required=True, help="destination player cache directory")
    args = parser.parse_args()

    temporary: tempfile.TemporaryDirectory[str] | None = None
    try:
        if args.brres:
            brres_path = args.brres.resolve()
            temporary = tempfile.TemporaryDirectory(prefix="sshd-link-")
            dae_directory = Path(temporary.name)
            export_brres(brres_path, dae_directory)
        else:
            dae_directory = args.dae_directory.resolve()
            brres_path = dae_directory / "model.brres"

        xtx_path = args.xtx.resolve() if args.xtx else brres_path.with_name("model.xtx")
        default_animation_brres = brres_path.parents[1].parent / "PLCommonAnime" / "g3d" / "model.brres"
        animation_brres = (
            args.animation_brres.resolve() if args.animation_brres else default_animation_brres
        )
        if not brres_path.is_file():
            raise FileNotFoundError(f"missing Alink BRRES used for texture ordering: {brres_path}")
        if not xtx_path.is_file():
            raise FileNotFoundError(f"missing Alink HD texture bundle: {xtx_path}")
        if not animation_brres.is_file():
            raise FileNotFoundError(
                f"missing PLCommonAnime BRRES (pass --animation-brres): {animation_brres}"
            )

        face = dae_directory / "Link-al_face.dae"
        variants = (
            (
                "TUNIC_0 green hero",
                dae_directory / "Link-al.dae",
                dae_directory / "Link-al_head.dae",
                dae_directory / "Link-al_hands.dae",
                "Link_al.glb",
            ),
            (
                "TUNIC_1 opening Skyloft",
                dae_directory / "Link-pl_body.dae",
                dae_directory / "Link-pl_head.dae",
                dae_directory / "Link-pl_hands.dae",
                "Link_pl.glb",
            ),
        )
        output_directory = args.output.resolve()
        for name, body, head, hands, filename in variants:
            destination = output_directory / filename
            primitive_count, triangle_count, texture_count = build_outfit(
                name=name,
                body=body,
                face=face,
                head=head,
                hands=hands,
                brres_path=brres_path,
                animation_brres=animation_brres,
                xtx_path=xtx_path,
                output=destination,
            )
            print(
                f"Wrote {destination} ({primitive_count} primitives, {triangle_count} triangles, "
                f"{texture_count} original game textures)"
            )
        return 0
    finally:
        if temporary is not None:
            temporary.cleanup()


if __name__ == "__main__":
    raise SystemExit(main())
