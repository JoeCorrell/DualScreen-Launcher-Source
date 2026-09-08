"""Verify native stat-name/index tables against exact disk and live evidence.

This is an offline audit, NOT a runtime reader and NOT authorization to publish
unverified counter scopes or call native functions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

EXE_SHA='f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e'
TEXT_SHA='e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f'
RDATA_SHA='376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe'


def checked(path, expected):
    data=path.read_bytes()
    if hashlib.sha256(data).hexdigest()!=expected:
        raise ValueError('evidence hash mismatch: '+str(path))
    return data


def audit(exe, capture):
    disk=checked(exe,EXE_SHA)
    text=checked(capture/'loaded-text.bin',TEXT_SHA)
    rdata=checked(capture/'loaded-rdata.bin',RDATA_SHA)
    pe=struct.unpack_from('<I',disk,60)[0]
    count=struct.unpack_from('<H',disk,pe+6)[0]
    optional=pe+24
    base=struct.unpack_from('<I',disk,optional+28)[0]
    if base!=0x37000000:
        raise ValueError('unexpected image base')
    sections=optional+struct.unpack_from('<H',disk,pe+20)[0]
    def offset(va, length):
        for i in range(count):
            _,_,rva,size,raw=struct.unpack_from('<8sIIII',disk,sections+i*40)
            if base+rva<=va and va+length<=base+rva+size:
                return raw+va-base-rva
        raise ValueError('VA outside raw sections')
    tables=[]
    # These exact instruction sequences establish index -> name and index ->
    # counter offset. The table extents are corroborated by the hashing loop
    # at 3840b580: integer end 3b826d38, float end 3b826d9c.
    for kind,table,items,name_getter,value_getter,expected_value,field in (
        ('integer',0x3b826930,258,0x3840ca00,0x3840cfb0,'8b4424048b84812c020000c20400',0x22c),
        ('float',0x3b826d38,25,0x3840ca10,0x3840cfc0,'8b442404d9848134060000c20400',0x634)):
        name_code=bytes.fromhex('8b4424048b0485')+struct.pack('<I',table)+b'\xc3'
        actual=text[name_getter-base-0x1000:name_getter-base-0x1000+len(name_code)]
        if actual!=name_code:
            raise ValueError('native name lookup mismatch')
        value_code=bytes.fromhex(expected_value)
        actual=text[value_getter-base-0x1000:value_getter-base-0x1000+len(value_code)]
        if actual!=value_code:
            raise ValueError('native value getter mismatch: '+kind)
        raw=offset(table,items*4)
        names=[]
        for index in range(items):
            pointer=struct.unpack_from('<I',disk,raw+index*4)[0]
            position=pointer-0x3987f000
            if not 0<=position<len(rdata)-128:
                raise ValueError('name outside captured rdata')
            end=rdata.find(b'\0',position,position+128)
            if end<0:
                raise ValueError('unterminated stat name')
            name=rdata[position:end].decode('ascii')
            if not name.upper().startswith('EIPS_' if kind=='integer' else 'EFPS_'):
                raise ValueError('unexpected stat family')
            names.append({'index':index,'name':name,'nameAddress':hex(pointer),'counterOffset':hex(field+index*4)})
        tables.append({'kind':kind,'tableAddress':hex(table),'tableSha256':hashlib.sha256(disk[raw:raw+items*4]).hexdigest(),
                       'nameGetter':hex(name_getter),'valueGetter':hex(value_getter),'entries':names})
    return {'sourceExeSha256':EXE_SHA,'loadedTextSha256':TEXT_SHA,'loadedRdataSha256':RDATA_SHA,
            'runtimePublicationVerified':False,'scope':'Index mapping only; lifetime, scope, units and live updates require validation','tables':tables}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    result=audit(args.exe,args.capture)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,indent=2)
    print('Verified 258 integer and 25 float name/index mappings; no runtime publication enabled')


if __name__=='__main__':main()
