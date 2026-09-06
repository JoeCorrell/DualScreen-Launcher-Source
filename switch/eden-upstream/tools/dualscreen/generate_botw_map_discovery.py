"""Generate map discovery metadata from BOTW resources, never from a user's save."""
import argparse
import json
from pathlib import Path
import zlib
import oead


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--bootup', type=Path, required=True)
    p.add_argument('--existing-labels', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--flags-output', type=Path, required=True)
    a = p.parse_args()
    pack = oead.Sarc(a.bootup.read_bytes())
    def byml(name):
        b = bytes(pack.get_file(name).data)
        return oead.byml.from_binary(oead.yaz0.decompress(b) if b[:4] == b'Yaz0' else b)
    static = byml('Map/MainField/Static.smubin')
    areas = byml('UI/MapTex/MainFieldArea.byaml')
    flags = {f'MapTower_{i+1:02d}' for i in range(len(areas['List']))}
    labels = []
    for category in json.loads(a.existing_labels.read_text()):
        for layer in category['layers']:
            for m in layer['markers']:
                labels.append((category['name'],m['name'],m['coords'][1]/2,-m['coords'][0]/2))
    # These map existing translated presentation labels/icons only. Coordinates and
    # eligibility come exclusively from the game's LocationMarker/SaveFlag records.
    categories = {'Tower':'Sheikah Tower','Dungeon':'Shrine','Hatago':'Stable',
                  'Village':'Village','CheckPoint':'Village'}
    markers = []
    for entry in static['LocationMarker']:
        if 'SaveFlag' not in entry or 'Translate' not in entry: continue
        flag = str(entry['SaveFlag'])
        if not flag: continue
        flags.add(flag)
        icon = str(entry['Icon']) if 'Icon' in entry else ''
        point = entry['Translate']
        x,z = float(point['X']),float(point['Z'])
        message = str(entry['MessageID']) if 'MessageID' in entry else flag
        category = categories.get(icon, icon)
        candidates = [v for v in labels if v[0] == category]
        nearest = min(candidates,key=lambda v:(x-v[2])**2+(z-v[3])**2) if candidates else None
        name = nearest[1] if nearest and (x-nearest[2])**2+(z-nearest[3])**2 < 300**2 else message
        markers.append(dict(id=message,name=name,flag=flag,icon=icon,category=category,x=x,z=z))
    # Named places carry their own saved discovery flags, separate from terrain reveal.
    for entry in static['LocationPointer']:
        if 'SaveFlag' in entry and str(entry['SaveFlag']): flags.add(str(entry['SaveFlag']))
    # SaveFlag is a key, not a bool type declaration. Location_* discoveries are
    # saved s32 values; MapTower_* terrain unlocks are bools. Resolve from the
    # same game's schema so new markers cannot silently use the wrong buffer.
    game_data = bytes(pack.get_file('GameData/gamedata.ssarc').data)
    game_data = oead.Sarc(oead.yaz0.decompress(game_data)
                          if game_data[:4] == b'Yaz0' else game_data)
    types = {}
    for resource in game_data.get_files():
        document = oead.byml.from_binary(bytes(resource.data))
        for kind, records in document.items():
            for record in records:
                name = str(record['DataName'])
                if name in flags:
                    if kind not in ('bool_data', 's32_data'):
                        raise ValueError(f'Unsupported discovery type: {name}: {kind}')
                    types[name] = 'Bool' if kind == 'bool_data' else 'S32'
    if flags - types.keys():
        raise ValueError(f'Missing GameData schema for {sorted(flags - types.keys())}')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps({'source':'Bootup.pack/Map/MainField/Static.smubin',
        'towerCount':len(areas['List']),'markers':markers},indent=2)+'\n')
    a.flags_output.write_text('// Generated from native map SaveFlag records; no save values.\n'
        'enum class BotwMapFlagType { Bool, S32 };\n'
        'struct BotwMapFlag { std::string_view name; u32 hash; BotwMapFlagType type; };\n'
        'constexpr std::array<BotwMapFlag, '+str(len(flags))+'> BotwMapFlags{{\n'+
        ''.join('    {"'+flag+'", 0x'+format(zlib.crc32(flag.encode()),'08X')+
                'U, BotwMapFlagType::'+types[flag]+'},\n'
            for flag in sorted(flags))+'}};\n')
    print(f'Generated {len(markers)} native markers and {len(flags)} discovery flag keys')


if __name__ == '__main__': main()
