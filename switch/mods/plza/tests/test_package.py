"""Test the same JAR verifier shipped in Android against real publisher-signed code."""
import argparse, os, subprocess, tempfile, zipfile, hashlib, json
from pathlib import Path

root=Path(__file__).resolve().parents[4]
def main():
    ap=argparse.ArgumentParser();ap.add_argument('package', type=Path);args=ap.parse_args()
    java=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))/'bin'
    work=Path(tempfile.mkdtemp(prefix='verify-',dir=root/'.work/plza-package'))
    subprocess.run([str(java/'javac.exe'),'-d',str(work),
        str(root/'switch/companion-api/src/main/java/com/dualscreen/companion/api/SignedModuleArchive.java'),
        str(Path(__file__).with_name('VerifyArchive.java'))],check=True)
    certificate='21cca830bc0bc8beb13f0e689279f5a5999772b424ea2ef7dbca38582149502d'
    def verify(path, case, success=False, trust=certificate):
        subprocess.run([str(java/'java.exe'),'-Xmx256m','-cp',str(work),'VerifyArchive',str(path),str(work/case),trust,str(success).lower()],check=True)
    verify(args.package,'valid',True)
    verify(args.package,'untrusted',trust='0'*64)
    with zipfile.ZipFile(args.package) as source:
        manifest=json.loads(source.read('module.json'))
        assert len(manifest['buildIds'][0])==64
        for name,digest in manifest['files'].items(): assert hashlib.sha256(source.read(name)).hexdigest()==digest
        assert len(manifest['files'])==824
        entries={x:source.read(x) for x in source.namelist()}
        altered_manifest=dict(manifest, revision=manifest['revision']+1)
        altered_bytes=json.dumps(altered_manifest,separators=(',',':')).encode()
        assert altered_bytes != entries['module.json']
        for case,extra,replace in [
            ('unsigned',{'assets/unsigned.txt':b'unsigned'},{}),
            ('traversal',{'assets/../../outside':b'escape'},{}),
            ('unexpected',{'evil.dex':b'code'},{}),
            ('tampered',{}, {'lib/arm64-v8a/libmodule.so':b'not the signed native code'}),
            ('manifest_changed',{}, {'module.json':altered_bytes}),
            ('missing_signature',{},{}),
        ]:
            archive=work/(case+'.dsmod')
            with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as out:
                for name,data in entries.items():
                    if case=='missing_signature' and name.startswith('META-INF/'): continue
                    out.writestr(name,replace.get(name,data))
                for name,data in extra.items():out.writestr(name,data)
            verify(archive,case)
    print('PASS 8 signed archive cases; all 824 native/UI/asset hashes; 64-byte hex build ID')

if __name__=='__main__': main()
