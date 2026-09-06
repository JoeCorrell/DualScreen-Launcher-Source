"""Build Z-A independently: no launcher assemble task and no libyuzu rebuild.

Requires the local Android SDK/NDK, JDK17, publisher keystore.properties, and
the existing extracted plza assets. Generated outputs stay under .work.
"""
import argparse, hashlib, json, os, re, subprocess, zipfile
from pathlib import Path

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--revision', type=int, required=True)
    ap.add_argument('--skip-build', action='store_true', help='Package already built matching native/UI outputs')
    ap.add_argument('--skip-ui-build', action='store_true', help='Native-only update using the unchanged, previously built UI AAR')
    args=ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}',args.version) or args.revision<1: ap.error('Invalid version/revision')
    root=Path(__file__).resolve().parents[3]
    sdk=Path(os.environ.get('ANDROID_HOME',Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'))
    java=Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))
    env=os.environ.copy();env['JAVA_HOME']=str(java);env['PATH']=str(java/'bin')+os.pathsep+env['PATH']
    work=root/'.work/plza-package';work.mkdir(parents=True,exist_ok=True)
    def run(cmd): subprocess.run([str(x) for x in cmd],cwd=root,env=env,check=True)
    cmake=sdk/'cmake/3.31.6/bin/cmake.exe';ninja=sdk/'cmake/3.31.6/bin/ninja.exe'
    if not args.skip_build:
        run([cmake,'-S',root/'switch/mods/plza/native','-B',work/'native','-G','Ninja',
             '-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'ndk/29.0.14206865/build/cmake/android.toolchain.cmake'),
             '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release',
             '-DPLZA_PACKAGE_VERSION='+args.version])
        run([ninja,'-C',work/'native','-j1'])
        if not args.skip_ui_build:
            run([root/'gradlew.bat',':switch:mods:plza:ui:assembleRelease','--no-daemon','--max-workers=1',
                 '-Dorg.gradle.jvmargs=-Xmx1536m -XX:MaxMetaspaceSize=384m -XX:+UseSerialGC -XX:ActiveProcessorCount=2 -Dfile.encoding=UTF-8','-Pkotlin.compiler.execution.strategy=in-process'])
    with zipfile.ZipFile(root/'switch/mods/plza/ui/build/outputs/aar/ui-release.aar') as aar:
        (work/'classes.jar').write_bytes(aar.read('classes.jar'))
    dex=work/'ui.jar'
    if dex.exists(): dex.unlink() # Exact generated output, never user assets.
    run([sdk/'build-tools/36.0.0/d8.bat','--release','--min-api','26','--lib',sdk/'platforms/android-36/android.jar',
         '--output',dex,work/'classes.jar'])
    native=work/'libmodule.so'
    run([sdk/'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe','--strip-debug',
         '-o',native,work/'native/libplza_module.so'])
    readelf=sdk/'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
    dynamic=subprocess.check_output([str(readelf),'-d',str(native)],text=True)
    dependencies=re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic)
    assert dependencies and set(dependencies)<= {'libc.so','libm.so','libdl.so','liblog.so'}, dependencies
    assets=root/'switch/eden-upstream/src/android/app/src/dualscreen/assets/plza'
    entries={'ui.jar':dex,'lib/arm64-v8a/libmodule.so':native}
    entries.update({'assets/plza/'+p.relative_to(assets).as_posix():p for p in sorted(assets.rglob('*')) if p.is_file()})
    for name in ['map_texture_00.png','map_icons/player.png','item_names.tsv','pokedex.tsv']:
        assert 'assets/plza/'+name in entries, 'Missing required extracted asset '+name
    manifest={'schema':1,'hostAbi':1,'titleId':'0100F43008C44000','version':args.version,'revision':args.revision,
              'architecture':'arm64-v8a','entryClass':'com.dualscreen.mods.plza.PlzaCompanionView',
              'buildIds':['B1F12FD919EAE86AB8A978317677E64BCE443D1F000000000000000000000000'],
              'files':{name:hashlib.sha256(p.read_bytes()).hexdigest() for name,p in entries.items()}}
    unsigned=work/'unsigned.jar'
    with zipfile.ZipFile(unsigned,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('module.json',json.dumps(manifest,separators=(',',':')))
        for name,p in entries.items(): z.write(p,name)
    result=work/f'plza-{args.version}.dsmod'
    run([java/'bin/java.exe',root/'switch/mods/plza/SignPackage.java',root,unsigned,result])
    print(json.dumps({'package':str(result),'bytes':result.stat().st_size,'sha256':hashlib.sha256(result.read_bytes()).hexdigest()}))

if __name__=='__main__': main()
