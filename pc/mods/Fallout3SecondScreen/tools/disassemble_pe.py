#!/usr/bin/env python3
"""Disassemble a bounded RVA range from a PE image for interoperability work."""

import argparse

import capstone
import pefile


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("rva", type=lambda value: int(value, 0))
    parser.add_argument("size", type=lambda value: int(value, 0))
    parser.add_argument(
        "--calls-to",
        type=lambda value: int(value, 0),
        help="only print direct calls to this virtual address",
    )
    args = parser.parse_args()

    pe = pefile.PE(args.image, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    data = pe.get_data(args.rva, args.size)
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = False
    decoder.skipdata = True
    for instruction in decoder.disasm(data, image_base + args.rva):
        if args.calls_to is not None:
            if instruction.mnemonic != "call" or instruction.op_str != hex(args.calls_to):
                continue
        print(
            f"{instruction.address:08X}  {instruction.bytes.hex(' '):<28} "
            f"{instruction.mnemonic:<8} {instruction.op_str}"
        )


if __name__ == "__main__":
    main()
