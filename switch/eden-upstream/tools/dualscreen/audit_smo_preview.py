"""Validate every base-game wardrobe combination against the actual packaged GLBs."""
import argparse
import json
import math
import struct
from pathlib import Path
from plan_smo_preview import catalog


def audit(root, tables):
    index = json.loads((root / 'index.json').read_text(encoding='utf-8'))
    assert index['version'] == 2
    expected = catalog(tables['tables'])
    assert index['catalog'] == expected, 'Catalog differs from the native ItemList'
    loaded = {}

    def model(name):
        if name in loaded:
            return loaded[name]
        raw = (root / (name + '.glb')).read_bytes()
        assert struct.unpack_from('<III', raw) == (0x46546c67, 2, len(raw)), name
        length, kind = struct.unpack_from('<II', raw, 12)
        assert kind == 0x4e4f534a
        doc = json.loads(raw[20:20+length])
        assert len(doc['buffers']) == 1 and 'uri' not in doc['buffers'][0], name
        assert all('bufferView' in im and 'uri' not in im for im in doc.get('images', [])), name
        assert doc.get('animations') or doc['extras']['smoPreview']['static'], name
        assert any('mesh' in node for node in doc['nodes']), f'No visible geometry: {name}'
        for accessor in doc.get('accessors', []):
            for bound in ('min','max'):
                assert all(math.isfinite(v) for v in accessor.get(bound, [])), name
        loaded[name] = doc
        return doc

    combinations = 0
    for body in expected['bodies']:
        for hat in expected['heads']:
            recipe = index['appearances'][body][hat]
            doc = model(recipe['model'])
            joints = {n.get('name') for n in doc['nodes']}
            assert 1 <= len(recipe['parts']) <= 11
            for part in recipe['parts']:
                model(part['model'])
                assert part['joint'] is None or part['joint'] in joints, (body, hat, part)
                for key in ('rotation','translation','scale'):
                    assert len(part[key]) == 3 and all(math.isfinite(v) for v in part[key])
            combinations += 1
    print(f'PASS: {combinations} native combinations; {len(loaded)} complete models; '
          'all attachment joints, poses and embedded textures resolved')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('root', type=Path)
    p.add_argument('tables', type=Path)
    a = p.parse_args()
    audit(a.root, json.loads(a.tables.read_text(encoding='utf-8')))
