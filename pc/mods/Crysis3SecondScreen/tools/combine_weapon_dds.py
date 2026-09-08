"""Reassemble bounded Crysis DDS mip chunks; never modify extracted inputs.

Only accepts legacy 2D DXT1/3/5 or ATI2/BC5 with exact header-derived mip lengths.
Chunk zero contains the DDS header and smallest mips; numbered chunks
contain successively larger mips. Reject anything outside that contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def combine(path):
    first = path.read_bytes()
    if len(first) < 128 or first[:4] != b'DDS ':
        raise ValueError('missing DDS header')
    word = lambda offset: struct.unpack_from('<I', first, offset)[0]
    if word(4) != 124 or word(76) != 32 or word(112) or word(24):
        raise ValueError('not a supported legacy 2D DDS')
    width, height, mips = word(16), word(12), word(28)
    if not (word(8) & 0x20000) and mips == 0:
        mips = 1  # DDSD_MIPMAPCOUNT absent: one base-level image.
    # Microsoft DDS format documentation: ATI2 is BC5, 16 bytes per 4x4 block.
    block = {b'DXT1': 8, b'DXT3': 16, b'DXT5': 16, b'ATI2': 16}.get(first[84:88])
    if not block or not 1 <= width <= 4096 or not 1 <= height <= 4096 or not 1 <= mips <= 13:
        raise ValueError('unsupported dimensions, mips or encoding')
    sizes = [max(1, ((width >> i) + 3) // 4) * max(1, ((height >> i) + 3) // 4) * block for i in range(mips)]
    siblings = list(path.parent.glob(path.name[:-1] + '*'))
    numbered = {}
    for sibling in siblings:
        suffix = sibling.suffix[1:]
        if suffix.isdigit():
            numbered[int(suffix)] = sibling
    count = len(numbered)
    if sorted(numbered) != list(range(count)) or count > mips:
        raise ValueError('missing or excessive mip chunks')
    chunks = [numbered[i].read_bytes() for i in range(count - 1, 0, -1)]
    if any(len(chunk) != sizes[i] for i, chunk in enumerate(chunks)):
        raise ValueError('chunk size does not match mip dimensions')
    if len(first) - 128 != sum(sizes[count - 1:]):
        raise ValueError('resident mip tail size mismatch')
    return first[:128] + b''.join(chunks) + first[128:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    report = []
    for source in sorted(args.source.rglob('*.dds.0')):
        relative = source.relative_to(args.source).with_suffix('')
        try:
            data = combine(source)
        except ValueError as error:
            report.append({'path': str(relative), 'rejected': str(error)})
            continue
        target = args.output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        report.append({'path': str(relative), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    (args.output / 'dds-report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({'combined': sum('sha256' in row for row in report), 'rejected': sum('rejected' in row for row in report)}))


if __name__ == '__main__':
    main()
