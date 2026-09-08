"""Generate presentation metadata from verified base/patch item XML, never ownership."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
from decode_xml import decode


def build(workspace):
    items = {}
    provenance = []
    for tree, prefix in [('Scripts', ''), ('Patch', 'C3/')]:
        root = workspace/'.research/crysis3/extracted'/tree
        evidence = json.loads((root/'_extraction-evidence.json').read_text())
        if not evidence.get('allEntryCrcsValid'): raise ValueError('Unverified extraction')
        manifest = {entry['name']: entry for entry in evidence['files']}
        for path in sorted((root/prefix/'Scripts/Entities/Items/Crysis2/Weapons').rglob('*.xml')):
            relative = path.relative_to(root).as_posix()
            raw = path.read_bytes()
            record = manifest.get(relative)
            if record is None or len(raw)!=record['bytes'] or f'{zlib.crc32(raw)&0xffffffff:08x}'!=record['crc']:
                raise ValueError('Item extraction mismatch: '+relative)
            element = decode(raw)
            if element.tag!='item': continue
            name = element.get('name')
            if not name: continue
            category = element.get('category', '')
            selectable = None
            display = ''
            for param in element.findall('./params/param'):
                if param.get('GAME') not in (None, 'SP'): continue
                if param.get('name')=='selectable': selectable=param.get('value')=='1'
                if param.get('name')=='display_name': display=param.get('value', '')
            # Patch files override the same real item definition, not runtime state.
            items[name] = (category, selectable, display, relative, hashlib.sha256(raw).hexdigest())
        provenance.append(evidence['sha256'])
    return items, provenance


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--workspace', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args=p.parse_args()
    items, archives=build(args.workspace)
    lines=['#pragma once', '// Generated from the owner\'s verified item XML; not live ownership.',
           '#include <string_view>', 'namespace c3::weapon_catalog {',
           'struct Entry { std::string_view name,category; int selectable; std::string_view displayKey,source,sha256; };',
           'inline constexpr Entry Entries[]{']
    for name,(category,selectable,display,source,digest) in sorted(items.items()):
        lines.append('    {'+','.join([json.dumps(name),json.dumps(category),str(-1 if selectable is None else int(selectable)),json.dumps(display),json.dumps(source),json.dumps(digest)])+'},')
    lines+=['};', 'inline const Entry* Find(std::string_view name) {',
            '    for(const auto& entry:Entries) if(entry.name==name) return &entry;',
            '    return nullptr;', '}', '// Base and patch archive hashes: '+', '.join(archives),
            '} // namespace c3::weapon_catalog', '']
    args.output.write_text('\n'.join(lines), encoding='utf-8')
    print(f'Generated {len(items)} item definitions; no runtime ownership assigned')
