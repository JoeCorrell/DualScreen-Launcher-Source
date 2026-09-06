#!/usr/bin/env python3
"""Export local Odyssey BYML tables, retaining scenario/visibility data verbatim.

Private investigation output only. Presence in this index NEVER means the player
has discovered or unlocked an entry. This does not implement visibility logic.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path

import oead

ARCHIVES = ('WorldList', 'CheckpointFlagInfo', 'ShineInfo', 'CostumeList',
            'ItemList', 'HackObjList')


def plain(value):
    if isinstance(value, oead.byml.Hash):
        return {key: plain(child) for key, child in value.items()}
    if isinstance(value, oead.byml.Array):
        return [plain(child) for child in value]
    if value is None or isinstance(value, (str, bool, int)):
        return value
    if isinstance(value, (oead.S32, oead.U32, oead.S64, oead.U64)):
        return int(value)
    if isinstance(value, (float, oead.F32, oead.F64)):
        number = float(value)
        if not math.isfinite(number):
            raise ValueError('Non-finite BYML number')
        return number
    raise ValueError(f'Unsupported BYML type: {type(value).__name__}')


def export(romfs, output):
    tables, sources = {}, {}
    for archive in ARCHIVES:
        relative = f'SystemData/{archive}.szs'
        data = (romfs / relative).read_bytes()
        if len(data) > 32 * 1024 * 1024 or data[:4] != b'Yaz0':
            raise ValueError(f'Unexpected source archive: {relative}')
        if int.from_bytes(data[4:8], 'big') > 64 * 1024 * 1024:
            raise ValueError('Oversized uncompressed archive')
        sarc = oead.Sarc(oead.yaz0.decompress(data))
        members = list(sarc.get_files())
        if len(members) > 1024:
            raise ValueError('Too many table members')
        sources[relative] = hashlib.sha256(data).hexdigest()
        for member in members:
            if not member.name.endswith('.byml'):
                continue
            key = relative + ':' + member.name
            if key in tables:
                raise ValueError(f'Duplicate table: {key}')
            raw = bytes(member.data)
            tables[key] = plain(oead.byml.from_binary(raw))
            sources[key] = hashlib.sha256(raw).hexdigest()
    # Keep source order: different native tables do not necessarily use the same
    # world ordering. Do not derive world IDs from this export's dictionary order.
    result = {'schema': 1, 'source': 'locally extracted Odyssey base RomFS',
              'runtimeVisibilityResolved': False, 'sourceSha256': sources, 'tables': tables}
    encoded = json.dumps(result, ensure_ascii=False, allow_nan=False, indent=2)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(encoded + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('romfs', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = export(args.romfs, args.output)
    print(f'Exported {len(result["tables"])} native tables with source hashes; visibility unresolved')


if __name__ == '__main__':
    main()
