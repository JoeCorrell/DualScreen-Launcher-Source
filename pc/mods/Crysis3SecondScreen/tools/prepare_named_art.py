"""Export exact named game icon frames; no invented weapon/module identities."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import xml.etree.ElementTree as ET
import zlib
from PIL import Image
from build_weapon_catalog import build


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--workspace', type=Path, required=True)
    p.add_argument('--ffdec', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--kind', choices=('weapons','modules'), required=True)
    args=p.parse_args()
    if args.kind=='modules':
        tree='UI'; relative='Libs/UI/General/perkicons.gfx'
        source=args.workspace/'.research/crysis3/extracted/Scripts/Scripts/HUD/SPPerksDef.xml'
        wanted={node.attrib['id'] for node in ET.fromstring(source.read_bytes()).findall('./Modules/Module')}
        if len(wanted)!=16: raise ValueError('Unexpected module list')
    else:
        tree='Patch'; relative='C3/Libs/UI/WeaponIcons.gfx'
        definitions,_=build(args.workspace)
        wanted={name for name,(category,selectable,*_) in definitions.items() if selectable!=0 and category!='utility'}
        wanted.update(('ExtendedClip','Silencer','Reflex','LaserSight','PistolLaserSight','SniperScope','AssaultScope','TechScope','ForeGrip'))
    root=args.workspace/'.research/crysis3/extracted'/tree
    evidence=json.loads((root/'_extraction-evidence.json').read_text())
    record=next(item for item in evidence['files'] if item['name']==relative)
    source=root/relative; raw=source.read_bytes()
    if not evidence['allEntryCrcsValid'] or len(raw)!=record['bytes'] or f'{zlib.crc32(raw)&0xffffffff:08x}'!=record['crc']:
        raise ValueError('Icon movie differs from verified extraction')
    args.output.mkdir(parents=True, exist_ok=False)
    symbols=args.output/'symbols.xml'
    command=['java','-Xmx512m','-jar',str(args.ffdec)]
    subprocess.run(command+['-swf2xml',str(source),str(symbols)],check=True,timeout=60)
    candidates=[]
    for sprite in ET.parse(symbols).getroot().findall('.//item[@type="DefineSpriteTag"]'):
        frame=1; labels={}
        for tag in sprite.find('subTags'):
            if tag.get('type')=='FrameLabelTag': labels.setdefault(tag.get('name').lower(),[]).append((tag.get('name'),frame))
            elif tag.get('type')=='ShowFrameTag': frame+=1
        matches={}
        for name in wanted:
            possible=labels.get(name.lower(),[])
            exact=[entry for entry in possible if entry[0]==name]
            if len(exact)==1: matches[name]=exact[0]
            elif len(possible)==1: matches[name]=possible[0]
        candidates.append((len(matches),int(sprite.get('spriteId')),matches))
    candidates.sort(reverse=True)
    count,sprite,matches=candidates[0]
    if not count or (len(candidates)>1 and count==candidates[1][0]): raise ValueError('Ambiguous icon sprite')
    if args.kind=='modules' and set(matches)!=wanted: raise ValueError('Missing module icon')
    selection=str(sprite)+':'+','.join(str(value[1]) for _,value in sorted(matches.items()))
    frames=args.output/'frames'
    subprocess.run(command+['-selectid',str(sprite),'-select',selection,'-ignorebackground','-export','sprite',str(frames),str(source)],check=True,timeout=120)
    items=[]
    for name,(label,frame) in sorted(matches.items()):
        if not name.replace('_','').replace('-','').isalnum(): raise ValueError('Unsafe asset name')
        png=frames/f'DefineSprite_{sprite}'/f'{frame}.png'
        if not png.is_file():
            # Named exported sprites use their linkage directory in JPEXS.
            candidates=list(frames.glob('*/'+str(frame)+'.png'))
            if len(candidates)!=1: raise ValueError('Ambiguous exported frame path')
            png=candidates[0]
        with Image.open(png) as image:
            pixels=image.convert('RGBA')
            if not 0<pixels.width<=2048 or not 0<pixels.height<=2048 or not pixels.getchannel('A').getbbox():
                raise ValueError('Invalid or empty icon')
            data=struct.pack('<4sIII',b'C3RA',1,pixels.width,pixels.height)+pixels.tobytes()
        (args.output/(name+'.rgba')).write_bytes(data)
        items.append(dict(name=name,label=label,frame=frame,width=pixels.width,height=pixels.height,sha256=hashlib.sha256(data).hexdigest()))
    result=dict(kind=args.kind,source=relative,sourceSha256=hashlib.sha256(raw).hexdigest(),archiveSha256=evidence['sha256'],sprite=sprite,items=items,unmatched=sorted(wanted-set(matches)))
    (args.output/'art-evidence.json').write_text(json.dumps(result,indent=2))
    print(f'Exported {count} verified named {args.kind} icons; {len(result["unmatched"])} unmatched names are not substituted')


if __name__=='__main__': main()
