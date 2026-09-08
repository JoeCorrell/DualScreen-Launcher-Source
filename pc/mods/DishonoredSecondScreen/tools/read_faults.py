"""Decode the payload's bounded native exception records without executing game code."""
import argparse
import json
import struct
from pathlib import Path

FORMAT = struct.Struct("<4IQ14I")
FIELDS = ("magic", "size", "code", "flags", "tick", "thread", "address", "eip", "esp", "ebp",
          "eax", "ebx", "ecx", "edx", "esi", "edi", "informationCount", "information0", "information1")

def read(path):
    data = path.read_bytes()
    if len(data) > 1024 * 1024:
        raise ValueError("Fault file exceeds the bounded format")
    for offset in range(0, len(data), FORMAT.size):
        if len(data) - offset < FORMAT.size:
            yield {"offset": offset, "incompleteTailBytes": len(data) - offset}
            return
        row = dict(zip(FIELDS, FORMAT.unpack_from(data, offset)))
        if row.pop("magic") != 0x31464844 or row.pop("size") != FORMAT.size:
            raise ValueError(f"Unrecognized record at {offset}")
        for field in ("code", "flags", "address", "eip", "esp", "ebp", "eax", "ebx", "ecx", "edx", "esi", "edi", "information0", "information1"):
            row[field] = f"0x{row[field]:08x}"
        yield row

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("file", type=Path)
    args = parser.parse_args()
    for record in read(args.file):
        print(json.dumps(record))
