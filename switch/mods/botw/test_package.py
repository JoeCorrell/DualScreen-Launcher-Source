"""Validate the actual signed BOTW artifact using the Android host's verifier."""
import argparse, hashlib, io, json, os, struct, subprocess, tempfile, zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]

def dex_classes(data):
    assert data[:4]==b'dex\n'
    strings=struct.unpack_from('<I',data,0x3c)[0]
    types=struct.unpack_from('<I',data,0x44)[0]
    count,offset=struct.unpack_from('<II',data,0x60)
    for i in range(count):
        type_index=struct.unpack_from('<I',data,offset+i*32)[0]
        string_index=struct.unpack_from('<I',data,types+type_index*4)[0]
        at=struct.unpack_from('<I',data,strings+string_index*4)[0]
        while data[at]&128: at+=1
        at+=1
        yield data[at:data.index(b'\0',at)].decode('utf-8')

def main():
    p=argparse.ArgumentParser();p.add_argument('package',type=Path);a=p.parse_args()
    work=Path(tempfile.mkdtemp(prefix='verify-',dir=ROOT/'.work/botw-package'))
    java=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))/'bin'
    subprocess.run([str(java/'javac.exe'),'-J-Xmx128m','-d',str(work),
        str(ROOT/'switch/companion-api/src/main/java/com/dualscreen/companion/api/SignedModuleArchive.java'),
        str(ROOT/'switch/mods/plza/tests/VerifyArchive.java')],check=True)
    trust='21cca830bc0bc8beb13f0e689279f5a5999772b424ea2ef7dbca38582149502d'
    def verify(path,name,success=False,certificate=trust):
        subprocess.run([str(java/'java.exe'),'-Xmx128m','-XX:+UseSerialGC','-XX:ActiveProcessorCount=2',
            '-cp',str(work),'VerifyArchive',str(path.resolve()),str(work/name),certificate,str(success).lower()],check=True)
    verify(a.package,'valid',True)
    verify(a.package,'wrong-publisher',certificate='0'*64)
    with zipfile.ZipFile(a.package) as archive:
        manifest=json.loads(archive.read('module.json'))
        assert manifest['titleId']=='01007EF00011E000'
        assert manifest['hostFeatures']==['query64-rgba-v1']
        assert manifest['entryClass']=='com.dualscreen.mods.botw.BotwModuleView'
        assert len(manifest['buildIds'])==1 and len(manifest['buildIds'][0])==64
        files=manifest['files']
        overlays=ROOT/'switch/mods/botw/assets'
        added={ 'assets/'+p.relative_to(overlays).as_posix() for p in overlays.rglob('*') if p.is_file() }
        if manifest['version'] != '1.0.0':
            assert len(files)==248+len(added-{'assets/botw/player/animations.json'})
            assert added <= files.keys(), 'Presentation assets missing from signed package'
        else:
            assert len(files)==248
        for name,digest in files.items(): assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
        with zipfile.ZipFile(io.BytesIO(archive.read('ui.jar'))) as dex:
            classes=[c for name in dex.namelist() if name.endswith('.dex') for c in dex_classes(dex.read(name))]
        assert 'Lcom/dualscreen/mods/botw/BotwModuleView;' in classes
        assert classes and all(c.startswith('Lcom/dualscreen/mods/botw/') for c in classes), 'Duplicated host or Kotlin runtime classes'
        print(f'PASS: {len(files)} file hashes; {len(classes)} module-only DEX classes; manifest/build/host feature checks')
        for case,replace,extra in [
            ('tampered-native',{'lib/arm64-v8a/libmodule.so':b'tampered'},{}),
            ('tampered-manifest',{'module.json':json.dumps(dict(manifest,revision=99)).encode()},{}),
            ('unsigned-extra',{}, {'assets/unsigned.txt':b'unsigned'}),
            ('unsafe-path',{}, {'assets/../../escape':b'escape'}),
        ]:
            altered=work/(case+'.dsmod')
            with zipfile.ZipFile(altered,'w',zipfile.ZIP_DEFLATED) as out:
                for name in archive.namelist(): out.writestr(name,replace.get(name,archive.read(name)))
                for name,data in extra.items(): out.writestr(name,data)
            verify(altered,case)
    print('PASS: signed BOTW package accepted; wrong publisher, tampering, unsigned extras and traversal rejected')

if __name__=='__main__': main()
