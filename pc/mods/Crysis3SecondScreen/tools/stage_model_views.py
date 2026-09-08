"""Stage fixed-key cached game-mesh views and refresh only the Crysis manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--views',type=Path,required=True)
    p.add_argument('--models',type=Path,required=True)
    p.add_argument('--dll',type=Path,required=True)
    args=p.parse_args()
    workspace=Path(__file__).resolve().parents[4]
    mod=workspace/'launcher/src/main/assets/mods/crysis3'
    report=[]
    # All inputs validate before any staged payload changes.
    for key in ('SCAR','Hammer'):
        path=args.views/(key+'.rgba');data=path.read_bytes()
        if len(data)!=16+900*300*4 or struct.unpack_from('<4I',data)!=(0x41523343,1,900,300):
            raise ValueError('invalid cached model view: '+key)
        model=args.models/(key+'.c3m');evidence=json.loads(model.with_suffix('.json').read_text())
        if hashlib.sha256(model.read_bytes()).hexdigest()!=evidence['sha256'] or not evidence.get('sourceTextures'):
            raise ValueError('unverified model source: '+key)
        report.append({'key':key,'sourceModelSha256':evidence['sha256'],'viewSha256':hashlib.sha256(data).hexdigest(),
                       'appearance':'fixed default mesh view; live attachments and skins not synchronized'})
    dll=args.dll.read_bytes()
    if len(dll)<1024 or dll[:2]!=b'MZ':raise ValueError('invalid DLL input')
    target=mod/'payload/native/assets/model-views';target.mkdir(parents=True,exist_ok=True)
    for row in report:shutil.copyfile(args.views/(row['key']+'.rgba'),target/(row['key']+'.rgba'))
    shutil.copyfile(args.dll,mod/'payload/native/dinput8.dll')
    manifest_path=mod/'manifest.json';manifest=json.loads(manifest_path.read_text())
    manifest['version']='crysis3-readonly-ui-trial-2026.09.07.4'
    existing={e['source']:e for e in manifest['files']}
    for row in report:
        source='native/assets/model-views/'+row['key']+'.rgba'
        existing[source]={'source':source,'target':'Bin32/Crysis3PanelAssets/model-views/'+row['key']+'.rgba','sha256':row['viewSha256']}
    for entry in existing.values():
        entry['sha256']=hashlib.sha256((mod/'payload'/entry['source']).read_bytes()).hexdigest()
    manifest['files']=list(existing.values())
    manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    output=args.views/'model-view-evidence.json'
    output.write_text(json.dumps({'renderedFromActualGameMeshes':True,'liveAppearanceSynchronized':False,'items':report},indent=2)+'\n',encoding='utf-8')
    print('Staged two cached model views and native DLL; refreshed Crysis-only manifest. Not installed.')


if __name__=='__main__':main()
