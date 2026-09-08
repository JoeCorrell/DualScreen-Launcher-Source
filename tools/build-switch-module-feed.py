"""Add independently signed Switch packages to the existing automatic payload feed.

Never rebuild/replace unrelated runtime or mod packs. This produces local artifacts;
upload immutable packs first and publish payload-bootstrap.json last, after verification.
Keep the base manifest for rollback. Existing APK-only Switch descriptors are preserved
for older launchers; migrated launchers retain their bundled external routing descriptor.
"""
import argparse, hashlib, json, re, shutil, zipfile
from pathlib import Path

REPOSITORY='https://github.com/JoeCorrell/DualScreen-Launcher-Payloads'
TITLES={'01007EF00011E000':'botw','0100F43008C44000':'plza'}

def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True)
    p.add_argument('--version',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--module',type=Path,action='append',required=True);a=p.parse_args()
    base=json.loads(a.base.read_text(encoding='utf-8-sig'))
    assert base['schema']==1 and base['repository']==REPOSITORY
    assert re.fullmatch(r'\d+(?:\.\d+)+',a.version)
    assert tuple(map(int,a.version.split('.')))>tuple(map(int,base['version'].split('.')))
    assert re.fullmatch(r'[A-Za-z0-9._-]+',base['tag'])
    a.output.mkdir(parents=True,exist_ok=True)
    packs=list(base['packs']);seen=set();audit=[]
    for module in a.module:
        with zipfile.ZipFile(module) as z:
            info=json.loads(z.read('module.json'));root=TITLES[info['titleId']]
            assert root not in seen;seen.add(root)
            assert re.fullmatch(r'[A-Za-z0-9._-]+',info['version']) and info['revision']>0
            for name,expected in info['files'].items():
                with z.open(name) as stream: assert hashlib.file_digest(stream,'sha256').hexdigest()==expected,name
        name=f"{root}-module-{info['version']}-r{info['revision']}.zip"
        output=a.output/name
        # Never silently replace an already staged/published version.
        with zipfile.ZipFile(output,'x',zipfile.ZIP_STORED) as z:
            entry=zipfile.ZipInfo(root+'/package.dsmod',date_time=(1980,1,1,0,0,0))
            with z.open(entry,'w',force_zip64=True) as dest,module.open('rb') as source:
                shutil.copyfileobj(source,dest,1024*1024)
        expected=digest(module)
        with zipfile.ZipFile(output) as z:
            assert z.namelist()==[root+'/package.dsmod']
            with z.open(z.namelist()[0]) as stream: assert hashlib.file_digest(stream,'sha256').hexdigest()==expected
        descriptor=dict(name=name,root=root,bytes=output.stat().st_size,sha256=digest(output),
            url=f"{REPOSITORY}/releases/download/{base['tag']}/{name}")
        packs=[entry for entry in packs if entry['root']!=root]+[descriptor]
        audit.append(dict(title=info['titleId'],version=info['version'],revision=info['revision'],sha256=expected,pack=descriptor))
    untouched=[entry for entry in base['packs'] if entry['root'] not in seen]
    assert all(entry in packs for entry in untouched)
    assert len({entry['root'] for entry in packs})==len(packs)
    result=dict(base,version=a.version,packs=packs)
    (a.output/'payload-bootstrap.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    (a.output/'module-feed-audit.json').write_text(json.dumps(dict(previousVersion=base['version'],
        version=a.version,preservedPacks=untouched,modules=audit),indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(version=a.version,preservedRoots=[x['root'] for x in untouched],modules=audit),indent=2))

if __name__=='__main__':main()
