"""Locate game class registration records in the verified PE; read-only."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import pefile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("names", nargs="+")
parser.add_argument("--exe", type=Path, default=Path(".research/dishonored/original/Dishonored.exe"))
args = parser.parse_args()
data = args.exe.read_bytes()
if hashlib.sha256(data).hexdigest() != "67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5":
    raise SystemExit("Unsupported executable")
pe = pefile.PE(data=data, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
for name in args.names:
    needle = (name + "\0").encode("utf-16le")
    position = 0
    while (position := data.find(needle, position)) >= 0:
        name_va = pe.get_rva_from_offset(position) + base
        reference = 0
        while (reference := data.find(struct.pack("<I", name_va), reference)) >= 0:
            # The native game-package registration table uses this package
            # string address. Code references to a name are not table rows.
            if data[reference + 4:reference + 8] == struct.pack("<I", 0x010CC8A4):
                words = struct.unpack("<8I", data[reference:reference + 32])
                print(json.dumps({"class": name, "name": hex(name_va),
                    "registration": hex(pe.get_rva_from_offset(reference) + base),
                    "constructor": hex(words[5]), "words": [hex(word) for word in words]}))
            reference += 4
        position += len(needle)
