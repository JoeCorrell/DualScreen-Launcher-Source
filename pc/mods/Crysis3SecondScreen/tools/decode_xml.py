"""Decode acquired CryXmlB tables into a separate research tree.

Format corroboration: https://github.com/ExterraGroup/scdatatools/blob/master/scdatatools/cryxml/defs.py
This file's table extents, parent relationships and string offsets are checked;
the parser never modifies the extracted originals and never runs scripts.
"""
import argparse
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


def decode(data):
    if not data.startswith(b'CryXmlB\0'):
        return ET.fromstring(data)
    if len(data)<44:
        raise ValueError('Truncated CryXmlB header')
    size,no,nc,ao,ac,co,cc,so,ss=struct.unpack_from('<9I',data,8)
    if size!=len(data) or not nc or nc>1000000:
        raise ValueError('Invalid CryXmlB file length/node count')
    ranges=[(no,nc*28),(ao,ac*8),(co,cc*4),(so,ss)]
    for start,length in ranges:
        if start<44 or start+length>len(data):
            raise ValueError('CryXmlB table outside file')
    occupied=sorted((a,a+b) for a,b in ranges if b)
    if any(occupied[i][1]>occupied[i+1][0] for i in range(len(occupied)-1)):
        raise ValueError('Overlapping CryXmlB tables')
    def string(offset):
        if offset>=ss:
            raise ValueError('String outside table')
        end=data.find(b'\0',so+offset,so+ss)
        if end<0:
            raise ValueError('Unterminated CryXmlB string')
        return data[so+offset:end].decode('utf-8')
    nodes=[struct.unpack_from('<IIHHIIII',data,no+i*28) for i in range(nc)]
    seen=set()
    def node(index,parent,depth):
        if depth>128 or index>=nc or index in seen:
            raise ValueError('Cyclic/shared/out-of-range XML node')
        seen.add(index)
        tag,content,attrs,children,actual_parent,first_attr,first_child,_=nodes[index]
        if actual_parent!=parent or first_attr+attrs>ac or first_child+children>cc:
            raise ValueError('Invalid CryXmlB node relationship')
        result=ET.Element(string(tag)); result.text=string(content) or None
        for i in range(first_attr,first_attr+attrs):
            key,value=struct.unpack_from('<II',data,ao+i*8)
            name=string(key)
            if name in result.attrib:
                raise ValueError('Duplicate XML attribute')
            result.set(name,string(value))
        for i in range(first_child,first_child+children):
            child=struct.unpack_from('<I',data,co+i*4)[0]
            result.append(node(child,index,depth+1))
        return result
    result=node(0,0xffffffff,0)
    if len(seen)!=nc:
        raise ValueError('Unreachable XML nodes')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():
        raise SystemExit('Output must be a new directory')
    count=0
    for path in args.source.rglob('*.xml'):
        raw=path.read_bytes()
        if not raw.startswith(b'CryXmlB\0'):
            continue
        element=decode(raw); ET.indent(element)
        output=args.output/path.relative_to(args.source)
        output.parent.mkdir(parents=True,exist_ok=True)
        ET.ElementTree(element).write(output,encoding='utf-8',xml_declaration=True)
        count+=1
    print(f'Decoded and structurally verified {count} CryXmlB files')
