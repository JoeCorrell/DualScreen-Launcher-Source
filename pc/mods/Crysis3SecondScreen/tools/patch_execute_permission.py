"""Build a hash-locked, one-bit PE permission delta. Never edits the input EXE.

Evidence: 2026-09-07 Wine execute fault at 39C169CE, RVA 02C169CE,
inside this build's non-executable .data section containing x86 instructions.
This is a compatibility trial, not a verified gameplay fix or global DEP opt-out.
"""
import argparse
import gzip
import hashlib
from pathlib import Path
import struct

ORIGINAL_SHA = "f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e"
FAULT_RVA = 0x02C169CE
MAGIC = b"DSDP1\0\0\0"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def permission_offset(data):
    if data[:2] != b"MZ":
        raise ValueError("Missing DOS signature")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Missing PE signature")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    if machine != 0x14C or struct.unpack_from("<H", data, pe + 24)[0] != 0x10B:
        raise ValueError("Expected the verified x86 PE32 build")
    table = pe + 24 + optional_size
    for index in range(count):
        section = table + index * 40
        name = data[section:section + 8].rstrip(b"\0")
        size, rva = struct.unpack_from("<II", data, section + 8)
        if rva <= FAULT_RVA < rva + size:
            flags = struct.unpack_from("<I", data, section + 36)[0]
            if name != b".data" or rva != 0x2B40000 or flags != 0xC0000040:
                raise ValueError("Fault section does not match captured evidence")
            return section + 39  # high byte of Characteristics: C0 -> E0
    raise ValueError("Fault address is outside the image sections")


def patch(data):
    if digest(data) != ORIGINAL_SHA:
        raise ValueError("Unsupported executable; no bytes changed")
    offset = permission_offset(data)
    result = bytearray(data)
    result[offset] |= 0x20  # IMAGE_SCN_MEM_EXECUTE; retain existing R/W flags
    return bytes(result), offset


def delta(original, output, offset):
    if len(original) != len(output) or original[offset] ^ output[offset] != 0x20:
        raise ValueError("Expected exactly one execute permission bit")
    if original[:offset] != output[:offset] or original[offset + 1:] != output[offset + 1:]:
        raise ValueError("Unexpected code/data changes")
    header = MAGIC + struct.pack("<Q", len(original)) + bytes.fromhex(digest(original))
    header += struct.pack("<Q", len(output)) + bytes.fromhex(digest(output))
    ops = b"\0" + struct.pack("<QI", 0, offset)
    ops += b"\1" + struct.pack("<I", 1) + output[offset:offset + 1]
    ops += b"\0" + struct.pack("<QI", offset + 1, len(original) - offset - 1) + b"\xff"
    return gzip.compress(header + ops, mtime=0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--delta", type=Path, required=True)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve() or args.output.exists() or args.delta.exists():
        parser.error("Use new output paths; input and existing outputs are never overwritten")
    original = args.input.read_bytes()
    output, offset = patch(original)
    payload = delta(original, output, offset)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.delta.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    args.delta.write_bytes(payload)
    print(f"One byte at file offset {offset:#x}: {original[offset]:02x} -> {output[offset]:02x}")
    print(f"output SHA256 {digest(output)}")
    print(f"delta SHA256 {digest(payload)} ({len(payload)} bytes)")


if __name__ == "__main__":
    main()
