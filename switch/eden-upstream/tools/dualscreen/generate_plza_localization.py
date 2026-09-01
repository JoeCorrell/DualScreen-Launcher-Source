#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


def decrypt_lines(path: Path) -> list[str]:
    data = path.read_bytes()
    section_count, line_count, _, initial_key, section = struct.unpack_from("<HHIII", data)
    if section_count != 1 or initial_key != 0:
        raise RuntimeError(f"Invalid encrypted text table: {path}")
    result = []
    for line_index in range(line_count):
        relative, length, _ = struct.unpack_from("<IHH", data, section + 4 + line_index * 8)
        key = (0x7C89 + line_index * 0x2983) & 0xFFFF
        values = []
        for char_index in range(length):
            value = struct.unpack_from("<H", data, section + relative + char_index * 2)[0] ^ key
            key = ((key << 3) | (key >> 13)) & 0xFFFF
            values.append(value)
        result.append(render_line(values))
    return result


def render_line(values: list[int]) -> str:
    output = []
    index = 0
    while index < len(values):
        value = values[index]
        index += 1
        if value == 0:
            break
        if value != 0x10:
            output.append("\n" if value == 10 else chr(value))
            continue
        if index + 1 >= len(values):
            break
        count = values[index]
        variable = values[index + 1]
        index += 2
        if variable in (0xBE00, 0xBE01):
            output.append("\n")
        elif variable == 0xBE02 and index < len(values):
            index += 1
        elif variable == 0xFF01 and index + 1 < len(values):
            base_length = values[index]
            ruby_length = values[index + 1]
            index += 2
            output.extend(chr(char) for char in values[index : index + base_length])
            index += base_length + ruby_length + base_length
        else:
            index += max(0, count - 1)
    return re.sub(r"\s+", " ", "".join(output)).strip()


def read_labels(path: Path) -> list[str]:
    data = path.read_bytes()
    if data[:4] != b"AHTB":
        raise RuntimeError(f"Invalid AHTB: {path}")
    count = struct.unpack_from("<I", data, 4)[0]
    labels = []
    offset = 8
    for _ in range(count):
        length = struct.unpack_from("<H", data, offset + 8)[0]
        labels.append(data[offset + 10 : offset + 10 + length - 1].decode("utf-8"))
        offset += 10 + length
    return labels


def table(package: Path) -> dict[str, str]:
    dat = next(package.glob("*.dat"))
    ahtb = next(package.glob("*.ahtb"))
    lines = decrypt_lines(dat)
    labels = read_labels(ahtb)
    if len(labels) == len(lines) + 1 and labels[-1].endswith("_max"):
        labels.pop()
    if len(lines) != len(labels):
        raise RuntimeError(f"Label/text count mismatch in {package}")
    return dict(zip(labels, lines))


def escape_tsv(value: str) -> str:
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--pokemon-icons", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    item_package = args.input / "ik_messagedatEnglishcommonitemname.tbl.trpak"
    item_lines = decrypt_lines(next(item_package.glob("*.dat")))
    item_output = args.output / "item_names.tsv"
    item_output.write_text(
        "".join(f"{index}\t{escape_tsv(name)}\n" for index, name in enumerate(item_lines) if name),
        encoding="utf-8",
    )

    quest_rows = []
    objective_rows = []
    package_specs = (
        (
            "ik_messagedatEnglishcommonquestlist_main.tbl.trpak",
            "main",
            r"msg_mainquest_title_(\d+)_(\d+)",
            r"msg_mainquest_purpose_(\d+)_(\d+)_(\d+)",
        ),
        (
            "ik_messagedatEnglishcommonquestlist_sub.tbl.trpak",
            "side",
            r"msg_subquest_title_(\d+)",
            r"msg_subquest_purpose_(\d+)_(\d+)",
        ),
        (
            "ik_messagedatEnglishcommonquestlist_mj.tbl.trpak",
            "research",
            r"msg_mjquest_title_(\d+)",
            r"msg_mjquest_purpose_(\d+)_(\d+)",
        ),
    )
    for package_name, category, pattern, purpose_pattern in package_specs:
        messages = table(args.input / package_name)
        for label, title in messages.items():
            match = re.fullmatch(pattern, label)
            if not match or not title:
                continue
            quest_id = "-".join(match.groups())
            outline_label = label.replace("_title_", "_outline_")
            description = messages.get(outline_label, "")
            quest_rows.append((category, quest_id, escape_tsv(title), escape_tsv(description)))
        for label, objective in messages.items():
            match = re.fullmatch(purpose_pattern, label)
            if not match or not objective:
                continue
            groups = match.groups()
            quest_id = "-".join(groups[:-1]) if category == "main" else groups[0]
            objective_rows.append(
                (category, quest_id, str(int(groups[-1])), escape_tsv(objective))
            )
    quest_output = args.output / "quests.tsv"
    quest_output.write_text(
        "".join("\t".join(row) + "\n" for row in quest_rows),
        encoding="utf-8",
    )
    objective_output = args.output / "quest_objectives.tsv"
    objective_output.write_text(
        "".join("\t".join(row) + "\n" for row in objective_rows),
        encoding="utf-8",
    )

    pokedex_count = 0
    if args.pokemon_icons:
        icon_pattern = re.compile(r"pm(\d{4})_00_00_00_0\.png")
        species = sorted(
            {
                int(match.group(1))
                for path in args.pokemon_icons.glob("*.png")
                if (match := icon_pattern.fullmatch(path.name)) and int(match.group(1)) > 0
            }
        )
        names = table(args.input / "ik_messagedatEnglishcommonmonsname.tbl.trpak")
        categories = table(args.input / "ik_messagedatEnglishcommonzkn_type.tbl.trpak")
        heights = table(args.input / "ik_messagedatEnglishcommonzkn_height.tbl.trpak")
        weights = table(args.input / "ik_messagedatEnglishcommonzkn_weight.tbl.trpak")
        descriptions = table(args.input / "ik_messagedatEnglishcommonzukan_comment_A.tbl.trpak")
        pokedex_rows = []
        for species_id in species:
            base = f"{species_id:03d}"
            name = names.get(f"MONSNAME_{base}", "")
            if not name:
                continue
            pokedex_rows.append(
                (
                    str(species_id),
                    escape_tsv(name),
                    escape_tsv(categories.get(f"ZKN_TYPE_{base}", "")),
                    escape_tsv(heights.get(f"ZKN_HEIGHT_{base}_000", "")),
                    escape_tsv(weights.get(f"ZKN_WEIGHT_{base}_000", "")),
                    escape_tsv(descriptions.get(f"ZKN_COMMENT_A_{base}_000", "")),
                )
            )
        (args.output / "pokedex.tsv").write_text(
            "".join("\t".join(row) + "\n" for row in pokedex_rows),
            encoding="utf-8",
        )
        pokedex_count = len(pokedex_rows)

    print(
        f"Wrote {len(item_lines)} item slots, {len(quest_rows)} quests, "
        f"{len(objective_rows)} objectives, and {pokedex_count} Pokédex entries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
