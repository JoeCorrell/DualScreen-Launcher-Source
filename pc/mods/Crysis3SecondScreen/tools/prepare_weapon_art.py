"""Export named weapon-movie frames from owner-extracted game data.

Names identify artwork, not inventory, runtime IDs or available equipment.
Requires the local JPEXS CLI and Pillow. No game files are modified.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

from PIL import Image

LABELS = ('Hammer', 'Nova', 'SCAR', 'FragGrenades', 'C4',
          'ExtendedClip', 'Silencer', 'Reflex', 'Nanobow')


def named_frames(root):
    candidates = []
    for sprite in root.findall(".//item[@type='DefineSpriteTag']"):
        frame, labels = 1, {}
        for tag in sprite.find('subTags'):
            if tag.get('type') == 'FrameLabelTag':
                name = tag.get('name')
                if name in labels:
                    raise ValueError('Duplicate frame label')
                labels[name] = frame
            elif tag.get('type') == 'ShowFrameTag':
                frame += 1
        if all(name in labels for name in LABELS):
            candidates.append((int(sprite.get('spriteId')), labels))
    if len(candidates) != 1:
        raise ValueError('Expected exactly one sprite containing the requested named frames')
    return candidates[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch-root', type=Path, required=True)
    parser.add_argument('--ffdec', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source_name = 'C3/Libs/UI/WeaponIcons.gfx'
    source = args.patch_root / source_name
    evidence = json.loads((args.patch_root / '_extraction-evidence.json').read_text('utf-8'))
    entry = next(e for e in evidence['files'] if e['name'].replace('\\', '/') == source_name)
    raw = source.read_bytes()
    if not evidence['allEntryCrcsValid'] or len(raw) != entry['bytes'] or f'{zlib.crc32(raw):08x}' != entry['crc']:
        raise ValueError('Movie differs from verified extraction')
    args.output.mkdir(parents=True, exist_ok=False)
    xml_path = args.output / 'symbols.xml'
    command = ['java', '-Xmx512m', '-jar', str(args.ffdec)]
    subprocess.run(command + ['-swf2xml', str(source), str(xml_path)], check=True, timeout=60)
    sprite, frames = named_frames(ET.parse(xml_path).getroot())
    selection = str(sprite) + ':' + ','.join(str(frames[name]) for name in LABELS)
    png_root = args.output / 'frames'
    subprocess.run(command + ['-selectid', str(sprite), '-select', selection, '-ignorebackground',
                             '-export', 'sprite', str(png_root), str(source)], check=True, timeout=60)
    items = []
    for name in LABELS:
        png = png_root / f'DefineSprite_{sprite}' / f'{frames[name]}.png'
        with Image.open(png) as source_image:
            if not 0 < source_image.width <= 2048 or not 0 < source_image.height <= 2048:
                raise ValueError('Image exceeds renderer bounds')
            pixels = source_image.convert('RGBA')
            if not pixels.getchannel('A').getbbox():
                raise ValueError(f'Blank export: {name}')
            packed = struct.pack('<4sIII', b'C3RA', 1, pixels.width, pixels.height) + pixels.tobytes()
            (args.output / f'{name}.rgba').write_bytes(packed)
            items.append(dict(label=name, sprite=sprite, frame=frames[name],
                              width=pixels.width, height=pixels.height,
                              rgbaSha256=hashlib.sha256(packed).hexdigest(),
                              pngSha256=hashlib.sha256(png.read_bytes()).hexdigest()))
    manifest = dict(kind='static named game artwork; not runtime item ownership or ID mappings',
                    source=source_name, sourceSha256=hashlib.sha256(raw).hexdigest(),
                    archiveSha256=evidence['sha256'],
                    exporter=ET.parse(xml_path).getroot().get('_generator'), items=items)
    (args.output / 'art-evidence.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'Validated and exported {len(items)} named frames; no runtime mappings inferred')


if __name__ == '__main__':
    main()
