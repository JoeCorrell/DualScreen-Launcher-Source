"""Convert verified, owner-extracted DDS artwork into a bounded native RGBA asset.
No generated imagery; the source and output hashes are recorded. Originals stay
untouched. Output is private build data, never added to the payload source tree.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from PIL import Image

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.ui_root / 'Libs/UI/General/CreateProfile_Nanosuit.dds.0'
    evidence = json.loads((args.ui_root / '_extraction-evidence.json').read_text('utf-8'))
    name = source.relative_to(args.ui_root).as_posix()
    entry = next(e for e in evidence['files'] if e['name'].replace('\\','/') == name)
    import zlib
    raw = source.read_bytes()
    if not evidence['allEntryCrcsValid'] or len(raw) != entry['bytes'] or f'{zlib.crc32(raw):08x}' != entry['crc']:
        raise SystemExit('Artwork differs from verified extraction')
    with Image.open(source) as image:
        if image.width > 2048 or image.height > 2048:
            raise SystemExit('Artwork exceeds memory budget')
        pixels = image.convert('RGBA')
        args.output.mkdir(parents=True, exist_ok=True)
        packed = struct.pack('<4sIII', b'C3RA', 1, pixels.width, pixels.height) + pixels.tobytes()
        (args.output / 'nanosuit.rgba').write_bytes(packed)
        pixels.save(args.output / 'nanosuit.png')
    manifest = dict(kind='static original game artwork, not a live suit render', source=name,
                    archiveSha256=evidence['sha256'], sourceSha256=hashlib.sha256(raw).hexdigest(),
                    outputSha256=hashlib.sha256(packed).hexdigest(), width=pixels.width, height=pixels.height)
    (args.output / 'art-evidence.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest))
