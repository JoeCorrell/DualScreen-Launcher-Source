"""Pack selected, SHA-verified private captures for copied-byte reader replay.

This preserves separately timed captures, not an atomic runtime snapshot.
Usage: script.py CAPTURE_FOLDER NEW_OUTPUT_BIN COMMA_SEPARATED_READ_IDS
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    root = Path(sys.argv[1])
    ids = [int(value) for value in sys.argv[3].split(",")]
    if not ids or len(ids) > 256 or len(ids) != len(set(ids)):
        raise ValueError("Invalid capture selection")
    packed = bytearray(struct.pack("<II", 0x524F3343, len(ids)))
    for read_id in ids:
        folder = root / f"read{read_id}"
        metadata = json.loads((folder / f"read-{read_id}.json").read_text(encoding="utf-8"))
        data = (folder / f"read-{read_id}.bin").read_bytes()
        if metadata.get("id") != read_id or metadata.get("readError") != 0 or metadata.get("stable") is not True:
            raise ValueError("Read was not reported successful and stable")
        address = metadata["address"]
        if len(data) != metadata["requestedBytes"] or not 0 < len(data) <= 65536 or address < 65536 or address + len(data) > 2**32:
            raise ValueError("Invalid capture bounds")
        if hashlib.sha256(data).hexdigest() != metadata["sha256"]:
            raise ValueError("Capture content hash mismatch")
        packed.extend(struct.pack("<II", address, len(data)))
        packed.extend(data)
    if len(packed) > 2 * 1024 * 1024:
        raise ValueError("Replay exceeds bounded fixture size")
    with Path(sys.argv[2]).open("xb") as output:
        output.write(packed)
    print(f"Packed {len(ids)} independently timed captures; SHA256 {hashlib.sha256(packed).hexdigest()}")
