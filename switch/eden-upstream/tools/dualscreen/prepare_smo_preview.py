"""Package native BFRES-derived preview parts with interpolated glTF animations.

Consumes private outputs of Build-SmoPreview.ps1; only the builder's asset output
is packaged. Native InitPartsFixInfo provides attachments, not hand-authored offsets.
"""
from __future__ import annotations
import argparse
import json
import math
import struct
from pathlib import Path
from smo_preview_catalog import build_appearances


def decompose(m):
    if len(m) != 16 or not all(math.isfinite(v) for v in m):
        raise ValueError('Invalid animation matrix')
    scales = [math.sqrt(sum(m[c*4+r]**2 for r in range(3))) for c in range(3)]
    if min(scales) < 1e-8:
        # Native invisible joints can have zero scale. Keep them invisible without
        # producing NaNs; their rotation has no observable effect at this key.
        return m[12:15], [0,0,0,1], scales
    r = [[m[c*4+y] / scales[c] for c in range(3)] for y in range(3)]
    determinant = sum(r[0][i]*(r[1][(i+1)%3]*r[2][(i+2)%3]-r[1][(i+2)%3]*r[2][(i+1)%3]) for i in range(3))
    if determinant < 0:
        scales[0] *= -1
        for row in r: row[0] *= -1
    trace = r[0][0]+r[1][1]+r[2][2]
    if trace > 0:
        s = math.sqrt(trace+1)*2
        q = [(r[2][1]-r[1][2])/s, (r[0][2]-r[2][0])/s, (r[1][0]-r[0][1])/s, s/4]
    else:
        i = max(range(3), key=lambda n:r[n][n]); j=(i+1)%3; k=(i+2)%3
        s = math.sqrt(max(0, 1+r[i][i]-r[j][j]-r[k][k]))*2
        if s < 1e-8: raise ValueError('Degenerate rotation')
        q = [0.,0.,0.,(r[k][j]-r[j][k])/s]
        q[i]=s/4; q[j]=(r[i][j]+r[j][i])/s; q[k]=(r[i][k]+r[k][i])/s
    norm=math.sqrt(sum(v*v for v in q))
    return m[12:15], [v/norm for v in q], scales


def apply_native_visibility(doc, visibility):
    if visibility.get('schema') != 1 or visibility.get('clip') != 'Wait' or visibility.get('frame') != 0:
        raise ValueError('Unsupported native visibility sample')
    hidden = set(visibility['hiddenBones'])
    matched = set()
    for node in doc['nodes']:
        if 'mesh' not in node: continue
        mesh = doc['meshes'][node['mesh']]
        # Converter preserves native rigid eyelid shape/bone names before __material.
        name = mesh.get('name', '').split('__', 1)[0]
        if name in hidden:
            matched.add(name)
            node.pop('mesh'); node.pop('skin', None)
    if hidden != matched:
        raise ValueError(f'Native hidden shapes not resolved: {hidden - matched}')


def animate(source: Path, output: Path, *, native_static=False, preserve_hair=False):
    raw = source.read_bytes()
    magic, version, size = struct.unpack_from('<III', raw)
    if (magic,version,size) != (0x46546c67,2,len(raw)): raise ValueError('Invalid GLB')
    length, kind = struct.unpack_from('<II',raw,12)
    if kind != 0x4e4f534a: raise ValueError('Missing JSON')
    doc = json.loads(raw[20:20+length])
    offset=20+length
    length,kind = struct.unpack_from('<II',raw,offset)
    if kind != 0x004e4942: raise ValueError('Missing binary')
    binary=bytearray(raw[offset+8:offset+8+length])
    clip = None if native_static else json.loads(source.with_name('idle.json').read_text())['clips']['Wait']
    frames = [] if native_static else clip['frames']
    if not native_static and len(frames) < 2: raise ValueError('Missing idle samples')
    def accessor(values, components):
        binary.extend(b'\0'*((-len(binary))%4))
        start=len(binary)
        flat=[v for row in values for v in row]
        binary.extend(struct.pack('<'+'f'*len(flat),*flat))
        view=len(doc.setdefault('bufferViews',[]))
        doc['bufferViews'].append({'buffer':0,'byteOffset':start,'byteLength':len(flat)*4})
        index=len(doc.setdefault('accessors',[]))
        data={'bufferView':view,'componentType':5126,'count':len(values),
              'type':{1:'SCALAR',3:'VEC3',4:'VEC4'}[components]}
        if components==1: data.update(min=[min(flat)],max=[max(flat)])
        doc['accessors'].append(data)
        return index
    time=accessor([[f['timeMs']/1000] for f in frames],1) if frames else None
    animation={'name':'Wait','samplers':[],'channels':[]}
    for node_index,node in enumerate(doc['nodes']):
        if native_static: break
        name=node.get('name','')
        if name not in frames[0]['bones']: continue
        poses=[decompose(f['bones'][name]) for f in frames]
        node.pop('matrix',None)
        for component,path in enumerate(('translation','rotation','scale')):
            values=[p[component] for p in poses]
            if path=='rotation':
                for i in range(1,len(values)):
                    if sum(a*b for a,b in zip(values[i-1],values[i]))<0: values[i]=[-v for v in values[i]]
            node[path]=values[0]
            sampler=len(animation['samplers'])
            animation['samplers'].append({'input':time,'output':accessor(values,len(values[0])),'interpolation':'LINEAR'})
            animation['channels'].append({'sampler':sampler,'target':{'node':node_index,'path':path}})
    if not native_static and not animation['channels']: raise ValueError('Animation matches no model joints')
    if native_static: doc.pop('animations', None)
    else: doc['animations']=[animation]
    doc['buffers']=[{'byteLength':len(binary)}]
    doc.setdefault('extras',{})['smoPreview']={'clip':'Wait','sourceFps':60,'nativeParts':True,'static':native_static}
    # glTF optional collections must be omitted, not empty. Metal Mario has
    # native uniform materials with no image data; rigid parts have no clip.
    for key in ('images', 'textures', 'samplers'):
        if not doc.get(key): doc.pop(key, None)
    visibility_path = source.with_name('visibility.json')
    if source.stem == 'MarioEye' and not visibility_path.exists():
        raise ValueError('MarioEye requires native Wait visibility; skeletal animation alone leaves eyelids closed')
    if visibility_path.exists():
        visibility = json.loads(visibility_path.read_text(encoding='utf-8-sig'))
        apply_native_visibility(doc, visibility)
        doc['extras']['smoPreview']['visibility'] = visibility
    # Native head visibility: wearing a hat uses CapHair, not the hatless Hair mesh
    # (PlayerFunction::setupClosetPlayerModel, 0x447068..0x4470f4).
    if source.stem.endswith('Head') and not preserve_hair:
        hidden={i for i,m in enumerate(doc.get('meshes',[])) if m.get('name','').startswith('Hair__')}
        for node in doc['nodes']:
            if node.get('mesh') in hidden: node.pop('mesh',None); node.pop('skin',None)
    encoded=json.dumps(doc,separators=(',',':')).encode(); encoded+=b' '*((-len(encoded))%4)
    binary.extend(b'\0'*((-len(binary))%4))
    data=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))
    data+=struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary
    output.write_bytes(data)


def native_body_parts(info, available):
    sub = info.get('InitSubActorPauseMenu.byml') or info.get('InitSubActor.byml')
    if not sub or 'InitPartsFixInfoHead.byml' not in info:
        return None
    creators = [part for part in sub['CreatorList'] if part.get('ClassName') == 'PartsModel']
    # Common body initialization creates the equipped head separately (0x444538).
    # Retain this body's native attachment, not a copied Mario transform.
    if not any(p.get('FixFileSuffixName') == 'Head' for p in creators):
        creators.append({'FixFileSuffixName': 'Head', 'ModelName': ''})
    parts = []
    for part in creators:
        slot = part['FixFileSuffixName']
        model = part['ModelName']
        if slot != 'Head' and model not in available:
            raise ValueError(f'Missing required part {model}')
        fix = info['InitPartsFixInfo'+slot+'.byml']
        parts.append({'slot': slot, 'model': model, 'joint': 'Armature_'+fix['JointName'],
            'rotation': [fix.get('LocalRotate', {}).get(k, 0) for k in 'XYZ'],
            'translation': [fix.get('LocalTrans', {}).get(k, 0) for k in 'XYZ']})
    return parts


def prepare(models: Path, metadata: Path, output: Path, plan: Path | None = None):
    output.mkdir(parents=True,exist_ok=True)
    native=json.loads(metadata.read_text(encoding='utf-8'))
    if plan:
        required=json.loads(plan.read_text(encoding='utf-8'))
        available=set(required['models'])
        recipes=build_appearances(native, required['catalog'], available)
        for name in sorted(available):
            folder=models/name
            completion=json.loads((folder/'complete.json').read_text(encoding='utf-8-sig'))
            if completion['model'] != name: raise ValueError(f'Invalid conversion: {name}')
            animate(folder/(name+'.glb'), output/(name+'.glb'),
                    native_static=not completion['animated'], preserve_hair=True)
        (output/'index.json').write_text(json.dumps({'version':2, 'catalog':required['catalog'],
            'appearances':recipes},separators=(',',':')),encoding='utf-8')
        print(f'Prepared complete wardrobe: {len(available)} models / {sum(map(len,recipes.values()))} combinations')
        return
    available={p.parent.name for p in models.glob('*/*.glb') if p.with_name('idle.json').exists()}
    for name in sorted(available): animate(models/name/(name+'.glb'),output/(name+'.glb'))
    bodies={}; heads={}
    for name in sorted(available):
        info=native[name]['metadata']
        if name.endswith('Head'): heads[name[:-4]]=name
        parts = native_body_parts(info, available)
        if parts is None: continue
        bodies[name]={'model':name,'parts':parts}
    (output/'index.json').write_text(json.dumps({'version':1,'bodies':bodies,'heads':heads},indent=2))
    print(f'Prepared {len(available)} animated models, {len(bodies)} bodies, {len(heads)} hats')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('models',type=Path);p.add_argument('metadata',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--plan',type=Path)
    a=p.parse_args();prepare(a.models,a.metadata,a.output,a.plan)
