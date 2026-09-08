"""Build BOTW independently: no launcher assemble task and no libyuzu rebuild.

Requires the local Android SDK/NDK, JDK17, publisher keystore.properties, and
the existing extracted BOTW assets. Generated outputs stay under .work.
"""
import argparse, hashlib, json, os, re, subprocess, zipfile, tempfile
from pathlib import Path

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--version', required=True)
    ap.add_argument('--revision', type=int, required=True)
    ap.add_argument('--skip-build', action='store_true', help='Package already built matching native/UI outputs')
    ap.add_argument('--skip-ui-build', action='store_true', help='Native-only update using the unchanged, previously built UI AAR')
    ap.add_argument('--player-assets', type=Path, required=True, help='Validated generated BOTW player model directory (read-only input)')
    ap.add_argument('--low-memory-ui', action='store_true', help='Compile standalone UI with a 512-MB Kotlin compiler instead of configuring launcher Gradle projects')
    args=ap.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9._-]{1,64}',args.version) or args.revision<1: ap.error('Invalid version/revision')
    root=Path(__file__).resolve().parents[3]
    sdk=Path(os.environ.get('ANDROID_HOME',Path(os.environ['LOCALAPPDATA'])/'Android/Sdk'))
    java=Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))
    env=os.environ.copy();env['JAVA_HOME']=str(java);env['PATH']=str(java/'bin')+os.pathsep+env['PATH']
    # D8 and source-file signing otherwise size their JVM heap against all host RAM.
    env['JAVA_TOOL_OPTIONS']='-Xmx512m -XX:MaxMetaspaceSize=256m -XX:+UseSerialGC -XX:ActiveProcessorCount=2'
    work=root/'.work/botw-package';work.mkdir(parents=True,exist_ok=True)
    def run(cmd): subprocess.run([str(x) for x in cmd],cwd=root,env=env,check=True)
    cmake=sdk/'cmake/3.31.6/bin/cmake.exe';ninja=sdk/'cmake/3.31.6/bin/ninja.exe'
    if not args.skip_build:
        run([cmake,'-S',root/'switch/mods/botw/native','-B',work/'native','-G','Ninja',
             '-DCMAKE_MAKE_PROGRAM='+str(ninja),'-DCMAKE_TOOLCHAIN_FILE='+str(sdk/'ndk/29.0.14206865/build/cmake/android.toolchain.cmake'),
             '-DANDROID_ABI=arm64-v8a','-DANDROID_PLATFORM=android-26','-DANDROID_STL=c++_static','-DCMAKE_BUILD_TYPE=Release',
             '-DBOTW_PACKAGE_VERSION='+args.version])
        run([ninja,'-C',work/'native','-j1'])
        if not args.skip_ui_build and not args.low_memory_ui:
            run([root/'gradlew.bat',':switch:mods:botw:ui:assembleRelease',':switch:eden-runtime:verifyBotwBasePlayerAssets',
                 '-PbotwPlayerAssetsDir='+str(args.player_assets.resolve()),'--no-daemon','--max-workers=1','--no-parallel',
                 '-Dorg.gradle.jvmargs=-Xmx1536m -XX:MaxMetaspaceSize=384m -XX:+UseSerialGC -XX:ActiveProcessorCount=2 -Dfile.encoding=UTF-8','-Pkotlin.compiler.execution.strategy=in-process'])
    if args.low_memory_ui:
        from test_migration import contracts, asset_contracts, typecheck
        contracts(); asset_contracts(args.player_assets)
        # Always compile into a fresh directory: removed classes must not survive an update.
        ui_work=Path(tempfile.mkdtemp(prefix='ui-build-',dir=work))
        typecheck(ui_work)
        with zipfile.ZipFile(work/'classes.jar','w',zipfile.ZIP_DEFLATED) as jar:
            for p in sorted((ui_work/'classes').rglob('*.class')):
                name=p.relative_to(ui_work/'classes').as_posix()
                # Host API was a compile dependency, never a second copy in external DEX.
                if name.startswith('com/dualscreen/mods/botw/'): jar.write(p,name)
    else:
        with zipfile.ZipFile(root/'switch/mods/botw/ui/build/outputs/aar/ui-release.aar') as aar:
            (work/'classes.jar').write_bytes(aar.read('classes.jar'))
    dex=work/'ui.jar'
    if dex.exists(): dex.unlink() # Exact generated output, never user assets.
    run([sdk/'build-tools/36.0.0/d8.bat','--release','--min-api','26','--lib',sdk/'platforms/android-36/android.jar',
         '--output',dex,work/'classes.jar'])
    native=work/'libmodule.so'
    run([sdk/'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe','--strip-debug',
         '-o',native,work/'native/libbotw_module.so'])
    readelf=sdk/'ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
    dynamic=subprocess.check_output([str(readelf),'-d',str(native)],text=True)
    dependencies=re.findall(r'\(NEEDED\).*\[([^]]+)\]',dynamic)
    assert dependencies and set(dependencies)<= {'libc.so','libm.so','libdl.so','liblog.so'}, dependencies
    assets=root/'switch/eden-upstream/src/android/app/src/dualscreen/assets/botw'
    entries={'ui.jar':dex,'lib/arm64-v8a/libmodule.so':native}
    entries.update({'assets/botw/'+p.relative_to(assets).as_posix():p for p in sorted(assets.rglob('*')) if p.is_file()})
    # Module-only presentation updates must not change the legacy/shared host assets.
    module_assets = Path(__file__).resolve().parent/'assets'
    entries.update({'assets/'+p.relative_to(module_assets).as_posix():p
                    for p in sorted(module_assets.rglob('*')) if p.is_file()})
    # Same generated model set formerly included by the APK. Never move/delete inputs.
    for p in sorted(args.player_assets.glob('*.glb')):
        if p.name == 'Link.glb' or p.name.startswith('Armor_'):
            entries['assets/botw/player/'+p.name]=p
    for name in ['player/Link.glb','player/Armor_Default_Upper.glb','player/index.json','ui/map/locations.json']:
        assert 'assets/botw/'+name in entries, 'Missing required extracted asset '+name
    actors=json.loads(entries['assets/botw/player/index.json'].read_text(encoding='utf-8'))['actors']
    for actor, value in actors.items():
        model=value if isinstance(value,str) else value.get('file')
        # The working renderer deliberately supports cache-only weapon variants with
        # packaged fallbacks. Armor and held materials must always work on clean installs.
        if model and (actor.startswith(('Armor_','Item_','Animal_Insect_')) or actor=='Link'):
            assert 'assets/botw/player/'+model in entries, f'Missing indexed model {actor}: {model}'
    assert len(entries)<4999 and sum(p.stat().st_size for p in entries.values())<127*1024*1024, 'Package exceeds loader limits'
    manifest={'schema':1,'hostAbi':1,'hostFeatures':['query64-rgba-v1'],'titleId':'01007EF00011E000','version':args.version,'revision':args.revision,
              'architecture':'arm64-v8a','entryClass':'com.dualscreen.mods.botw.BotwModuleView',
              'buildIds':['CD57B23FA4BBAD65803D9788C01821EE00000000000000000000000000000000'],
              'files':{name:hashlib.sha256(p.read_bytes()).hexdigest() for name,p in entries.items()}}
    unsigned=work/'unsigned.jar'
    with zipfile.ZipFile(unsigned,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('module.json',json.dumps(manifest,separators=(',',':')))
        for name,p in entries.items(): z.write(p,name)
    result=work/f'botw-{args.version}.dsmod'
    run([java/'bin/java.exe',root/'switch/mods/plza/SignPackage.java',root,unsigned,result])
    print(json.dumps({'package':str(result),'bytes':result.stat().st_size,'sha256':hashlib.sha256(result.read_bytes()).hexdigest()}))

if __name__=='__main__': main()
