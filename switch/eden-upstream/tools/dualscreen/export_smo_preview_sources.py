"""Unpack private SMO preview sources and retain native dependency/attachment metadata.

Inputs are selected ObjectData archives extracted from the user's own base game.
No assets or absolute workspace paths belong in source control.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import oead


def plain(value):
    if isinstance(value, (oead.byml.Hash, dict)):
        return {str(k): plain(v) for k, v in value.items()}
    if isinstance(value, (oead.byml.Array, list)):
        return [plain(v) for v in value]
    if isinstance(value, (oead.S32, oead.U32, oead.S64, oead.U64)):
        return int(value)
    if isinstance(value, (oead.F32, oead.F64)):
        return float(value)
    return value


def export(source: Path, output: Path):
    output.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for path in sorted(source.glob('*.szs')):
        if not path.stem.startswith(('Mario', 'PlayerAnimation')):
            continue
        compressed = path.read_bytes()
        data = bytes(oead.yaz0.decompress(compressed)) if compressed[:4] == b'Yaz0' else compressed
        archive = oead.Sarc(data)  # Retain owner while iterating views.
        metadata = {}
        files = []
        for entry in archive.get_files():
            if Path(entry.name).name != entry.name:
                raise ValueError(f'Unexpected non-flat source: {entry.name}')
            if entry.name.endswith('.bfres'):
                (output / entry.name).write_bytes(bytes(entry.data))
                files.append(entry.name)
            elif entry.name.endswith('.byml'):
                metadata[entry.name] = plain(oead.byml.from_binary(bytes(entry.data)))
        manifest[path.stem] = {'files': files, 'metadata': metadata}
    (output / 'native-parts.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Exported {len(manifest)} native preview archives')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    export(args.source, args.output)
