#!/usr/bin/env python3



from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


def sign_extend(value: int, bits: int) -> int:
    sign_bit = 1 << (bits - 1)
    return (value ^ sign_bit) - sign_bit


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    parser.add_argument("text", help="ASCII string to locate")
    parser.add_argument("--text-size", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--look-ahead", type=int, default=8)
    parser.add_argument(
        "--window",
        type=lambda value: int(value, 0),
        default=0,
        help="also report references within this many bytes of a matching string",
    )
    parser.add_argument(
        "--dump-context",
        type=lambda value: int(value, 0),
        default=0,
        help="dump printable strings within this many bytes of each match",
    )
    args = parser.parse_args()

    image = args.image.read_bytes()
    needle = args.text.encode("ascii")
    targets = []
    start = 0
    while (offset := image.find(needle, start)) >= 0:
        targets.append(offset)
        start = offset + 1

    if not targets:
        print(f"String not found: {args.text}")
        return 1

    is_nso = image[:4] == b"NSO0"
    segments = (
        [struct.unpack_from("<IIII", image, 0x10 + index * 0x10) for index in range(3)]
        if is_nso
        else [(0, 0, len(image), 0)]
    )

    def file_to_virtual(offset: int) -> int:
        for file_offset, location, size, _ in segments:
            if file_offset <= offset < file_offset + size:
                return location + offset - file_offset
        raise ValueError(f"file offset 0x{offset:x} is outside the NSO segments")

    target_virtual = {file_to_virtual(target) for target in targets}
    references: list[tuple[int, int, int]] = []
    text_file_offset, text_location, header_text_size, _ = segments[0]
    text_size = min(args.text_size, header_text_size) & ~3
    for text_offset in range(0, text_size, 4):
        file_pc = text_file_offset + text_offset
        pc = text_location + text_offset
        instruction = struct.unpack_from("<I", image, file_pc)[0]
        if instruction & 0x9F000000 == 0x10000000:
            immediate = ((instruction >> 5) & 0x7FFFF) << 2
            immediate |= (instruction >> 29) & 0x3
            address = pc + sign_extend(immediate, 21)
            if any(abs(address - target) <= args.window for target in target_virtual):
                references.append((pc, pc, address))
            continue
        if instruction & 0x9F000000 != 0x90000000:
            continue
        register = instruction & 0x1F
        immediate = ((instruction >> 5) & 0x7FFFF) << 2
        immediate |= (instruction >> 29) & 0x3
        page = (pc & ~0xFFF) + (sign_extend(immediate, 21) << 12)
        chained_register = register
        chained_address = page
        for index in range(1, args.look_ahead + 1):
            add_pc = pc + index * 4
            add_file_pc = file_pc + index * 4
            if add_pc >= text_location + text_size:
                break
            add = struct.unpack_from("<I", image, add_file_pc)[0]
            if add & 0x7C000000 == 0x14000000:
                break
            if add & 0xFF000000 != 0x91000000:
                continue
            source_register = (add >> 5) & 0x1F
            destination_register = add & 0x1F
            shift = 12 if (add >> 22) & 1 else 0
            addend = ((add >> 10) & 0xFFF) << shift
            if source_register == chained_register:
                chained_register = destination_register
                chained_address += addend
                address = chained_address
            elif source_register == register:
                address = page + addend
            else:
                continue
            if any(abs(address - target) <= args.window for target in target_virtual):
                references.append((pc, add_pc, address))

    print(
        "strings:",
        ", ".join(
            f"file=0x{address:x} virtual=0x{file_to_virtual(address):x}" for address in targets
        ),
    )
    for target in targets:
        context_start = max(0, target - args.dump_context)
        context_end = min(len(image), target + len(needle) + args.dump_context)
        for match in re.finditer(rb"[\x20-\x7e]{3,}", image[context_start:context_end]):
            print(f"string: 0x{context_start + match.start():x} {match.group().decode('ascii')}")
    for adrp_pc, add_pc, address in references:
        print(f"xref: adrp=0x{adrp_pc:x} add=0x{add_pc:x} target=0x{address:x}")
    return 0 if references else 2


if __name__ == "__main__":
    raise SystemExit(main())
