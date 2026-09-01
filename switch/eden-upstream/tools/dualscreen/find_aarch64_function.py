#!/usr/bin/env python3


from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

from capstone import CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, Cs


def normalized(instruction) -> str:
    operands = re.sub(r"#-?0x[0-9a-f]+|#-?[0-9]+", "#imm", instruction.op_str)
    return f"{instruction.mnemonic} {operands}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("source_offset", type=lambda value: int(value, 0))
    parser.add_argument("--instructions", type=int, default=24)
    args = parser.parse_args()

    disassembler = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    source = args.source.read_bytes()
    target = args.target.read_bytes()

    def text_segment(image: bytes) -> tuple[int, int, int]:
        file_offset, location, size, _ = struct.unpack_from("<IIII", image, 0x10)
        return file_offset, location, size

    source_file, source_virtual, source_size = text_segment(source)
    target_file, target_virtual, target_size = text_segment(target)
    if not source_virtual <= args.source_offset < source_virtual + source_size:
        raise SystemExit("source offset is outside the source text segment")
    source_start = source_file + args.source_offset - source_virtual
    reference = list(
        disassembler.disasm(
            source[source_start : source_start + args.instructions * 4],
            args.source_offset,
        )
    )
    signature = [normalized(instruction) for instruction in reference]
    if len(signature) != args.instructions:
        raise SystemExit("source range did not decode completely")

    first_word = source[source_start : source_start + 4]
    candidates = []
    cursor = target_file
    target_end = target_file + target_size
    while True:
        cursor = target.find(first_word, cursor, target_end)
        if cursor < 0 or cursor + args.instructions * 4 > target_end:
            break
        if (cursor - target_file) % 4 == 0:
            virtual = target_virtual + cursor - target_file
            decoded = list(
                disassembler.disasm(
                    target[cursor : cursor + args.instructions * 4], virtual
                )
            )
            score = sum(
                left == normalized(right) for left, right in zip(signature, decoded)
            )
            candidates.append((score, virtual, decoded))
        cursor += 1

    for score, virtual, decoded in sorted(candidates, reverse=True)[:20]:
        print(f"{score:02}/{args.instructions} 0x{virtual:08X}")
        if score == args.instructions:
            break


if __name__ == "__main__":
    main()
