"""Resolve complete private preview input coverage from the base game's ItemList.

This catalog describes asset availability only, never runtime ownership/unlocks.
"""
import argparse
import json
from pathlib import Path
from smo_preview_catalog import appearance


def catalog(tables):
    return {kind: [r['ItemName'] for r in tables[f'SystemData/ItemList.szs:Item{table}.byml']]
            for kind, table in [('bodies', 'Cloth'), ('heads', 'Cap')]}


def make_plan(native, tables):
    expected = catalog(tables)
    models = set(expected['bodies'])
    # Invisible Cap is a game-owned special appearance, not a missing archive.
    models.update(n + 'Head' for n in expected['heads'] if n != 'MarioInvisible')
    for body in expected['bodies']:
        info = native[body]['metadata']
        sub = info.get('InitSubActorPauseMenu.byml') or info.get('InitSubActor.byml') or {}
        models.update(p['ModelName'] for p in sub.get('CreatorList', [])
                      if p.get('ClassName') == 'PartsModel')
        costume = info.get('InitCostumeInfoBody.byml') or {}
        if costume.get('IsUseBodyHair'):
            models.add(body + 'Hair')
    # Native initializer 0x444028 selects these body-dependent head resources.
    for name in ('Mario64HeadMetal', 'Mario64HeadOther', 'Mario64MetalHeadMetal',
                 'Mario64MetalHeadOther', 'MarioPeachHeadShort'):
        models.add(name)
    for body in expected['bodies']:
        for hat in expected['heads']:
            models.update(p['model'] for p in appearance(native, body, hat)['parts'])
    missing = models - native.keys()
    if missing:
        raise ValueError(f'Missing native archives: {sorted(missing)}')
    return {'version': 1, 'catalog': expected, 'models': sorted(models)}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('native', type=Path)
    p.add_argument('tables', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    plan = make_plan(json.loads(a.native.read_text(encoding='utf-8')),
                     json.loads(a.tables.read_text(encoding='utf-8'))['tables'])
    a.output.write_text(json.dumps(plan, indent=2), encoding='utf-8')
    print(f"Resolved {len(plan['models'])} models for "
          f"{len(plan['catalog']['bodies'])} outfits / {len(plan['catalog']['heads'])} hats")
