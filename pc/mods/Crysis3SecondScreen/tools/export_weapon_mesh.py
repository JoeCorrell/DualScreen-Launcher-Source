"""Bake a verified converted GLB rest pose into the bounded C3 mesh format.

Offline tool only. Output contains actual geometry, UVs and decoded game
textures, not invented weapon state. Source GLB hash is recorded separately.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import numpy as np
from PIL import Image


class Glb:
    def __init__(self, path):
        self.raw = path.read_bytes()
        magic, version, length = struct.unpack_from('<III', self.raw)
        if magic != 0x46546c67 or version != 2 or length != len(self.raw) or length > 128*1024*1024:
            raise ValueError('invalid GLB')
        size, kind = struct.unpack_from('<II', self.raw, 12)
        if kind != 0x4e4f534a:
            raise ValueError('missing JSON')
        self.doc = json.loads(self.raw[20:20+size])
        offset = 20+size
        size, kind = struct.unpack_from('<II', self.raw, offset)
        if kind != 0x004e4942 or offset+8+size != len(self.raw):
            raise ValueError('missing binary data')
        self.binary = self.raw[offset+8:]
        self.world = {}
        self.source_textures = {}
        def visit(index, parent, stack):
            if index in stack or index in self.world:
                raise ValueError('cyclic or multiply-parented node')
            node = self.doc['nodes'][index]
            if 'matrix' in node:
                local = np.array(node['matrix']).reshape(4,4).T
            else:
                x,y,z,w = node.get('rotation', [0,0,0,1])
                local = np.eye(4)
                local[:3,:3] = np.array([
                    [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                    [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                    [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]) @ np.diag(node.get('scale',[1,1,1]))
                local[:3,3] = node.get('translation',[0,0,0])
            self.world[index] = parent @ local
            for child in node.get('children',[]):
                visit(child, self.world[index], stack | {index})
        for index in self.doc['scenes'][self.doc.get('scene',0)]['nodes']:
            visit(index, np.eye(4), set())

    def view(self, index):
        view = self.doc['bufferViews'][index]
        if view.get('buffer',0) != 0:
            raise ValueError('external buffer')
        begin, length = view.get('byteOffset',0), view['byteLength']
        if begin < 0 or length < 0 or begin+length > len(self.binary):
            raise ValueError('buffer bounds')
        return self.binary[begin:begin+length]

    def accessor(self, index):
        a = self.doc['accessors'][index]
        if 'sparse' in a or a.get('normalized'):
            raise ValueError('unsupported accessor')
        dtype = {5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
        cols = {'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}[a['type']]
        view = self.doc['bufferViews'][a['bufferView']]
        unit = np.dtype(dtype).itemsize
        stride = view.get('byteStride', cols*unit)
        offset, count = a.get('byteOffset',0), a['count']
        data = self.view(a['bufferView'])
        if not 0 < count <= 262144 or offset+(count-1)*stride+cols*unit > len(data):
            raise ValueError('accessor bounds')
        return np.ndarray((count,cols), dtype=dtype, buffer=data, offset=offset, strides=(stride,unit)).copy()

    def texture(self, info):
        if info is None:
            return struct.pack('<II',0,0)
        if info.get('texCoord',0) != 0 or info.get('extensions'):
            raise ValueError('unsupported UV transform')
        image = self.doc['images'][self.doc['textures'][info['index']]['source']]
        if 'bufferView' not in image:
            raise ValueError('external image')
        with Image.open(io.BytesIO(self.view(image['bufferView']))) as source:
            if max(source.size) > 4096:
                raise ValueError('texture too large')
            rgba = source.convert('RGBA')
            rgba.thumbnail((512,512), Image.Resampling.LANCZOS)
            return struct.pack('<II',*rgba.size) + rgba.tobytes()

    def source_texture(self,path):
        data=path.read_bytes()
        self.source_textures[str(path)]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
        with Image.open(io.BytesIO(data)) as source:
            if max(source.size)>4096:
                raise ValueError('source texture too large')
            rgba=source.convert('RGBA')
            rgba.thumbnail((512,512),Image.Resampling.LANCZOS)
            return struct.pack('<II',*rgba.size)+rgba.tobytes()

    def bake(self, materials=None, placement=None, texture_root=None):
        parts, records = [], []
        for ni, world in self.world.items():
            node = self.doc['nodes'][ni]
            if 'mesh' not in node:
                continue
            for primitive in self.doc['meshes'][node['mesh']]['primitives']:
                if primitive.get('mode',4) != 4:
                    raise ValueError('not triangles')
                attrs = primitive['attributes']
                pos, normal = self.accessor(attrs['POSITION']), self.accessor(attrs['NORMAL'])
                points = np.c_[pos, np.ones(len(pos))]
                if 'skin' in node:
                    skin = self.doc['skins'][node['skin']]
                    binds = self.accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
                    matrices = np.array([self.world[j] @ bind for j,bind in zip(skin['joints'],binds)])
                    joints, weights = self.accessor(attrs['JOINTS_0']), self.accessor(attrs['WEIGHTS_0'])
                    if np.any(joints >= len(matrices)) or not np.allclose(weights.sum(axis=1),1,atol=.002):
                        raise ValueError('invalid skin weights')
                    transforms = (matrices[joints] * weights[:,:,None,None]).sum(axis=1)
                    points = np.einsum('nij,nj->ni',transforms,points)
                    normal = np.einsum('nij,nj->ni',np.linalg.inv(transforms[:,:3,:3]).transpose(0,2,1),normal)
                else:
                    points = points @ world.T
                    normal = normal @ np.linalg.inv(world[:3,:3])
                normal /= np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-10)
                if placement is not None:
                    points = points @ placement.T
                    normal = normal @ placement[:3,:3].T
                uv = self.accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(pos),2))
                vertices = np.c_[points[:,:3],normal,uv].astype('<f4')
                indices = self.accessor(primitive['indices']).flatten().astype('<u4')
                if len(vertices)>65536 or len(indices)>196608 or len(indices)%3 or np.any(indices>=len(vertices)) or not np.isfinite(vertices).all():
                    raise ValueError('invalid mesh bounds')
                material = self.doc['materials'][primitive['material']]
                pbr = material['pbrMetallicRoughness']
                spec = material.get('extensions',{}).get('KHR_materials_pbrSpecularGlossiness',{})
                factor = pbr.get('baseColorFactor',[1,1,1,1])
                sf = spec.get('specularFactor',[.04,.04,.04])
                flags = int(material.get('alphaMode','OPAQUE') != 'OPAQUE')
                if materials is not None:
                    source_material = materials[material['name']]
                    flags |= int('%DECAL' in source_material.get('StringGenMask',''))
                record = struct.pack('<II8f',len(vertices),len(indices),*factor,*sf,spec.get('glossinessFactor',.5))
                record += struct.pack('<I',flags)
                record += vertices.tobytes()+indices.tobytes()
                for slot,info in zip(('Diffuse','Bumpmap','Specular'),(pbr.get('baseColorTexture'),material.get('normalTexture'),spec.get('specularGlossinessTexture'))):
                    if texture_root is not None and materials is not None:
                        source_path=source_material.get('textures',{}).get(slot)
                        if source_path:
                            candidate=(texture_root / Path(source_path).with_suffix('.dds')).resolve()
                            if not candidate.is_relative_to(texture_root.resolve()):
                                raise ValueError('texture path escapes root')
                            record += self.source_texture(candidate)
                            continue
                    record += self.texture(info)
                parts.append(record)
                records.append({'node':node.get('name'), 'material':material.get('name'), 'vertices':len(vertices), 'triangles':len(indices)//3})
        if not 1 <= len(parts) <= 32:
            raise ValueError('invalid part count')
        output = struct.pack('<4I',0x4d334343,2,len(parts),0)+b''.join(parts)
        if len(output)>32*1024*1024:
            raise ValueError('model budget exceeded')
        return output, records


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input',type=Path)
    p.add_argument('output',type=Path)
    p.add_argument('--material',type=Path)
    p.add_argument('--texture-root',type=Path)
    p.add_argument('--attachment',type=Path)
    p.add_argument('--cdf',type=Path)
    p.add_argument('--socket')
    args = p.parse_args()
    if args.output.exists():
        raise ValueError('output already exists')
    glb = Glb(args.input)
    materials = None
    if args.material:
        materials = {m.get('Name'):{**m.attrib,'textures':{t.get('Map'):t.get('File') for t in m.iter('Texture')}} for m in ET.parse(args.material).iter('Material') if m.get('Name')}
    data, parts = glb.bake(materials,texture_root=args.texture_root)
    attachment_evidence = None
    attachment_textures = {}
    if args.attachment:
        if not args.cdf or not args.socket:
            raise ValueError('attachment requires CDF and exact socket')
        sockets = [s for s in ET.parse(args.cdf).iter('Attachment') if s.get('AName') == args.socket]
        if len(sockets)!=1 or sockets[0].get('Type')!='CA_BONE' or sockets[0].get('Flags')!='0':
            raise ValueError('not a unique enabled bone attachment')
        socket = sockets[0]
        if Path(socket.get('Binding','')).stem.lower() != args.attachment.stem.lower():
            raise ValueError('attachment does not match CDF binding')
        w,x,y,z = map(float,socket.attrib['Rotation'].split(','))
        local = np.eye(4)
        local[:3,:3] = [[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                       [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                       [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
        local[:3,3] = list(map(float,socket.attrib['Position'].split(',')))
        basis = np.array([[1,0,0,0],[0,0,1,0],[0,-1,0,0],[0,0,0,1]])
        attachment = Glb(args.attachment)
        extra, extra_parts = attachment.bake(materials,basis @ local @ basis.T,args.texture_root)
        attachment_textures = attachment.source_textures
        parts += extra_parts
        if len(parts)>32 or len(data)+len(extra)-16>32*1024*1024:
            raise ValueError('assembled model budget exceeded')
        data = struct.pack('<4I',0x4d334343,2,len(parts),0)+data[16:]+extra[16:]
        attachment_evidence = {'cdf':str(args.cdf),'cdfSha256':hashlib.sha256(args.cdf.read_bytes()).hexdigest(),
                               'socket':socket.attrib,'glbSha256':hashlib.sha256(attachment.raw).hexdigest(),
                               'scope':'CDF default pose only; not live attachment state'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(data)
    report = {'source':str(args.input),'sourceSha256':hashlib.sha256(glb.raw).hexdigest(), 'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'parts':parts}
    report['sourceTextures'] = {**glb.source_textures,**attachment_textures}
    if args.material:
        report['materialSha256'] = hashlib.sha256(args.material.read_bytes()).hexdigest()
    if attachment_evidence:
        report['attachment'] = attachment_evidence
    args.output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
