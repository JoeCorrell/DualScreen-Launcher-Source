#!/usr/bin/env python3



from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    import oead
except ModuleNotFoundError:
    oead = None


def sarc_data(data: bytes) -> oead.Sarc:
    if oead is None:
        raise RuntimeError("oead is required to read SARC archives")
    if data[:4] == b"Yaz0":
        data = oead.yaz0.decompress(data)
    return oead.Sarc(data)


def decode_msbt_text(data: bytes) -> dict[str, str]:
    if data[:8] != b"MsgStdBn" or data[8:10] not in {b"\xff\xfe", b"\xfe\xff"}:
        raise ValueError("Expected an MSBT file with a valid byte-order marker")
    endian = "<" if data[8:10] == b"\xff\xfe" else ">"
    text_encoding = "utf-16le" if endian == "<" else "utf-16be"
    section_count = struct.unpack_from(f"{endian}H", data, 0xE)[0]
    sections: dict[str, bytes] = {}
    offset = 0x20
    for _ in range(section_count):
        name = data[offset : offset + 4].decode("ascii")
        size = struct.unpack_from(f"{endian}I", data, offset + 4)[0]
        start = offset + 0x10
        sections[name] = data[start : start + size]
        offset = (start + size + 0xF) & ~0xF

    labels: dict[int, str] = {}
    label_section = sections["LBL1"]
    group_count = struct.unpack_from(f"{endian}I", label_section, 0)[0]
    for group in range(group_count):
        count, entry_offset = struct.unpack_from(f"{endian}II", label_section, 4 + group * 8)
        for _ in range(count):
            length = label_section[entry_offset]
            entry_offset += 1
            label = label_section[entry_offset : entry_offset + length].decode("utf-8")
            entry_offset += length
            index = struct.unpack_from(f"{endian}I", label_section, entry_offset)[0]
            entry_offset += 4
            labels[index] = label

    text_section = sections["TXT2"]
    text_count = struct.unpack_from(f"{endian}I", text_section, 0)[0]
    offsets = list(struct.unpack_from(f"{endian}{text_count}I", text_section, 4))
    result: dict[str, str] = {}
    for index, label in labels.items():
        start = offsets[index]
        end = offsets[index + 1] if index + 1 < len(offsets) else len(text_section)
        raw = text_section[start:end]
        clean = bytearray()
        cursor = 0
        while cursor + 2 <= len(raw):
            code = struct.unpack_from(f"{endian}H", raw, cursor)[0]
            if code == 0:
                break
            if code == 0x000E and cursor + 8 <= len(raw):
                payload_size = struct.unpack_from(f"{endian}H", raw, cursor + 6)[0]
                cursor += 8 + payload_size
                continue
            if code == 0x000F and cursor + 6 <= len(raw):
                cursor += 6
                continue
            clean.extend(raw[cursor : cursor + 2])
            cursor += 2
        result[label] = clean.decode(text_encoding, errors="replace").replace("\r", "")
    return result


def cpp_string(value: str) -> str:
    return (
        '"'
        + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
        + '"'
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bootup_language_pack", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    outer = sarc_data(args.bootup_language_pack.read_bytes())
    message_archive = bytes(outer.get_file("Message/Msg_USen.product.ssarc").data)
    messages = sarc_data(message_archive)

    fields: dict[str, dict[str, str]] = {}
    for file in messages.get_files():
        if not file.name.startswith("ActorType/") or not file.name.endswith(".msbt"):
            continue
        for label, value in decode_msbt_text(bytes(file.data)).items():
            for suffix, field in (("_Name", "name"), ("_Desc", "description")):
                if label.endswith(suffix):
                    fields.setdefault(label[: -len(suffix)], {})[field] = value
                    break

    rows = sorted(
        (actor, values.get("name", actor), values.get("description", ""))
        for actor, values in fields.items()
        if "name" in values
    )
    lines = [
        "// Generated from the user's unmodified BOTW Bootup_USen.pack.",
        "// Do not hand-edit; regenerate with tools/dualscreen/generate_botw_item_text.py.",
        f"constexpr std::array<ItemText, {len(rows)}> BotwItemText{{{{",
    ]
    for actor, name, description in rows:
        lines.append(
            f"    ItemText{{{cpp_string(actor)}, {cpp_string(name)}, {cpp_string(description)}}},"
        )
    lines.append("}};")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {len(rows)} localized BOTW item rows in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
