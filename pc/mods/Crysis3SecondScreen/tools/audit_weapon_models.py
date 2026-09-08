"""Inventory declared weapon geometry/material variants; never invent aliases.

Roots are supplied in precedence order (base, then patch). This is an offline
coverage report, not evidence of runtime ownership or successful rendering.
"""
import argparse
import hashlib
import json
from pathlib import Path
from decode_xml import decode


def collect(roots):
    records = {}
    for root in roots:
        if not root.is_dir():
            raise ValueError('Missing weapon script root: ' + str(root))
        for path in sorted(root.rglob('*.xml')):
            raw = path.read_bytes()
            item = decode(raw)
            if item.tag != 'item' or not item.get('name'):
                continue
            records[item.get('name')] = {
                'name': item.get('name'), 'inherits': item.get('inherit'),
                'category': item.get('category'), 'source': str(path),
                'sourceSha256': hashlib.sha256(raw).hexdigest(),
                'declaredGeometry': [dict(view=node.tag, **node.attrib)
                                     for group in item.findall('geometry')
                                     for node in group
                                     if node.tag in ('firstperson', 'thirdperson')],
            }
    return sorted(records.values(), key=lambda row: row['name'].casefold())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scripts', type=Path, action='append', required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists() or not args.assets.is_dir():
        parser.error('Use a fresh output and an existing extracted asset root')
    available = {str(path.relative_to(args.assets)).replace('\\', '/').casefold()
                 for path in args.assets.rglob('*') if path.is_file()}
    rows = collect(args.scripts)
    for row in rows:
        for geometry in row['declaredGeometry']:
            name = geometry.get('name', '').replace('\\', '/').casefold()
            geometry['assetPresent'] = name in available
            material = geometry.get('material', '').replace('\\', '/').casefold()
            if material:
                if not material.endswith('.mtl'):
                    material += '.mtl'
                geometry['materialPresent'] = material in available
    report = {'runtimeCoverageVerified': False, 'inheritanceResolved': False,
              'items': rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
        stream.write('\n')
    print(f'Audited {len(rows)} weapon definitions; runtime coverage not implied.')


if __name__ == '__main__':
    main()
