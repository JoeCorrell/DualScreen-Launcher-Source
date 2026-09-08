"""Index/extract selected owner-supplied CryPak entries via read-only ADB ranges.

Avoids pulling multi-gigabyte archives for a few UI assets. Validates CryPak
headers, all wrapped keys, table bounds, entry sizes and CRCs. No remote writes.
Selected-entry integrity is NOT a claim of full-archive signature verification.
"""
import argparse
import fnmatch
import hashlib
import json
import shlex
import struct
import subprocess
import zlib
from pathlib import Path, PurePosixPath
from extract_pak import Twofish, public_keys, unwrap


class Remote:
    def __init__(self, adb, device, path):
        self.command=[str(adb),'-s',device,'exec-out']
        self.path=path
        self.size=int(self.run('stat -c %s '+shlex.quote(path)))
        if self.size<2342 or self.size>8*1024**3:
            raise ValueError('Unsupported remote archive size')
    def run(self, command):
        return subprocess.run(self.command+[command],check=True,stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,timeout=60).stdout
    def read(self,offset,size):
        if offset<0 or size<0 or offset+size>self.size or size>32*1024**2:
            raise ValueError('Read outside archive or transfer budget')
        block=65536; first=offset//block; lead=offset%block
        count=(lead+size+block-1)//block
        raw=self.run(f'dd if={shlex.quote(self.path)} bs={block} skip={first} count={count} 2>/dev/null')
        result=raw[lead:lead+size]
        if len(result)!=size:
            raise ValueError('Short ADB range read')
        return result


def index(reader,executable,cipher):
    tail_offset=max(0,reader.size-65557)
    tail=reader.read(tail_offset,reader.size-tail_offset)
    end=tail.rfind(b'PK\x05\x06')
    if end<0 or end+22>len(tail): raise ValueError('EOCD missing')
    _,disk,first,count,total,size,offset,comment=struct.unpack_from('<4s4H2IH',tail,end)
    if disk or first or count!=total or offset+size!=tail_offset+end or end+22+comment!=len(tail):
        raise ValueError('Inconsistent archive directory')
    trailer=tail[end+22:]
    if len(trailer)!=2320 or struct.unpack_from('<IHH',trailer)!=(8,3,1) or \
        struct.unpack_from('<I',trailer,8)[0]!=132 or struct.unpack_from('<I',trailer,140)[0]!=2180:
        raise ValueError('Unsupported CryPak encryption header')
    keys=None
    for key_offset,key in public_keys(executable):
        try:
            keys=[unwrap(trailer[144+i*128:144+(i+1)*128],key) for i in range(17)]
            break
        except ValueError: pass
    if keys is None: raise ValueError('No verified executable key can read archive')
    raw=reader.read(offset,size)
    directory=cipher.ctr(raw,keys[1],keys[0])
    cursor=0; entries=[]; names_seen=set()
    for _ in range(total):
        if cursor+46>len(directory) or directory[cursor:cursor+4]!=b'PK\x01\x02':
            raise ValueError('Invalid central-directory entry')
        _,_,needed,flags,method,_,_,crc,packed,unpacked,names,extra,notes,disk,_,_,local=struct.unpack_from('<4s6H3I5H2I',directory,cursor)
        finish=cursor+46+names+extra+notes
        if finish>len(directory) or needed>20 or disk or flags&8: raise ValueError('Unsupported entry metadata')
        name=directory[cursor+46:cursor+46+names].decode('utf-8').replace('\\','/')
        path=PurePosixPath(name)
        if not name or path.is_absolute() or '..' in path.parts or ':' in name or '\0' in name or name.casefold() in names_seen:
            raise ValueError('Unsafe/duplicate archive path')
        names_seen.add(name.casefold())
        start=local+30+names+extra
        if start+packed>offset: raise ValueError('Entry overlaps directory')
        entries.append(dict(name=name,start=start,packed=packed,bytes=unpacked,method=method,crc=f'{crc:08x}'))
        cursor=finish
    if cursor!=len(directory): raise ValueError('Trailing directory data')
    return entries,keys,dict(directorySha256=hashlib.sha256(raw).hexdigest(),publicKeyFileOffset=key_offset),tail_offset,tail


def decode_entry(reader,entry,keys,cipher):
    packed,size,crc=entry['packed'],entry['bytes'],int(entry['crc'],16)
    if packed>32*1024**2 or size>64*1024**2: raise ValueError('Selected entry exceeds extraction budget')
    data=reader.read(entry['start'],packed)
    method=entry['method']
    if method in (13,14) and packed:
        iv=struct.pack('<4I',((packed<<12)^size)&0xffffffff,int(not packed),
                       ((packed<<12)^crc)&0xffffffff,packed^int(not size))
        data=cipher.ctr(data,keys[1+((~(crc>>2))&15)],iv)
    if method in (8,14) and (packed or size):
        inflater=zlib.decompressobj(-15); data=inflater.decompress(data,size+1)
        if not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
            raise ValueError('Invalid deflate stream')
    elif method not in (0,8,13,14): raise ValueError('Unsupported compression')
    if len(data)!=size or zlib.crc32(data)!=crc: raise ValueError('Entry CRC/length mismatch')
    return data


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--adb',type=Path,required=True); p.add_argument('--device',required=True)
    p.add_argument('--archive',required=True); p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--twofish',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
    p.add_argument('--match',action='append',default=[]); p.add_argument('--extract',action='store_true')
    args=p.parse_args()
    if args.output.exists(): raise SystemExit('Output must be a new directory')
    reader=Remote(args.adb,args.device,args.archive); cipher=Twofish(args.twofish)
    entries,keys,evidence,tail_offset,tail=index(reader,args.exe,cipher)
    selected=[e for e in entries if any(fnmatch.fnmatchcase(e['name'].lower(),pattern.lower()) for pattern in args.match)]
    results=[]
    if args.extract:
        if not args.match or sum(e['bytes'] for e in selected)>128*1024**2:
            raise SystemExit('Select a bounded set of assets explicitly')
        for entry in selected:
            data=decode_entry(reader,entry,keys,cipher)
            results.append((entry,data))
    if reader.read(tail_offset,len(tail))!=tail:
        raise SystemExit('Archive metadata changed during acquisition')
    args.output.mkdir(parents=True)
    for entry,data in results:
        target=args.output.joinpath(*PurePosixPath(entry['name']).parts)
        if entry['name'].endswith('/'): target.mkdir(parents=True,exist_ok=True)
        else:
            target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
        entry['extractedSha256']=hashlib.sha256(data).hexdigest()
    evidence.update(remoteArchive=args.archive,archiveBytes=reader.size,signatureValidated=False,
                    fullArchiveHashVerified=False,entries=entries,selected=selected,extracted=len(results))
    (args.output/'_remote-evidence.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    print(json.dumps(dict(archive=args.archive,entries=len(entries),selected=selected,extracted=len(results))))
