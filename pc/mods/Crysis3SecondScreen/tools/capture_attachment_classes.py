"""Resolve copied accessory class identities with bounded, read-only requests.

Input records must come from a freshly verified item+14 accessory vector, stride
0x74. This is separate-timing research evidence, never an atomic UI snapshot.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('adb','serial','session','output','records'):
        parser.add_argument('--'+key,required=True)
    parser.add_argument('--first-id',type=int,required=True)
    args=parser.parse_args()
    records=Path(args.records).read_bytes()
    if not records or len(records)%116 or len(records)>16*116:
        raise ValueError('Expected one to sixteen complete native accessory records')
    next_id=args.first_id
    if not 1<=next_id<=129-len(records)//116*2:
        raise ValueError('Insufficient remaining request IDs')
    def word(b,offset=0):return struct.unpack_from('<I',b,offset)[0]
    def read(address,size,reason):
        nonlocal next_id
        request=next_id;next_id+=1
        folder=Path(args.output)/('read'+str(request))
        subprocess.run([sys.executable,str(Path(__file__).with_name('read_live_inspector.py')),
                        '--adb',args.adb,'--serial',args.serial,'--session',args.session,
                        '--id',str(request),'--address',hex(address),'--bytes',str(size),
                        '--evidence',reason,'--output',str(folder)],check=True,timeout=40,
                       stdout=subprocess.PIPE)
        data=(folder/('read-'+str(request)+'.bin')).read_bytes()
        if len(data)!=size:raise ValueError('Incomplete read')
        return data
    for i in range(len(records)//116):
        address=word(records,i*116)
        cls=read(address,12,'Accessory record class identity/index '+str(i))
        if word(cls)!=0x398f4638:raise ValueError('Unsupported class identity')
        text=read(word(cls,8),64,'Accessory native class name/index '+str(i))
        end=text.find(b'\0')
        if end<1:raise ValueError('Unterminated/empty class name')
        name=text[:end].decode('ascii')
        if not all(ch.isalnum() or ch=='_' for ch in name):raise ValueError('Unexpected class name')
        print(json.dumps(dict(index=i,cls=hex(address),name=name,
                              defaultFlag=records[i*116+0x6a])),flush=True)
    print('NEXT_REQUEST_ID='+str(next_id),flush=True)


if __name__=='__main__':main()
