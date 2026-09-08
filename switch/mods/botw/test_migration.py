"""Small offline migration checks; never touches device data or builds an APK."""
import argparse, hashlib, json, os, subprocess, zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[3]
MOD=ROOT/'switch/mods/botw'

def function(text,name):
    start=text.index(name+'('); start=text.rfind('\n',0,start)+1
    end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

def contracts():
    # Compare against the committed working checkpoint, not a mutable duplicate.
    old=subprocess.check_output(['git','show','87a35dc:switch/eden-upstream/src/core/loader/nso.cpp'],cwd=ROOT).decode()
    new=(MOD/'native/hooks.cpp').read_text()
    for name in ['BuildBotwCarryGrabReadyHook','InstallBotwCompanionEquipHook']:
        assert function(old,name)==function(new,name), 'Gameplay hook changed: '+name
    state=(MOD/'native/state.cpp').read_text()
    original=subprocess.check_output(['git','show','87a35dc:switch/eden-upstream/src/android/app/src/main/jni/botw_companion.cpp'],cwd=ROOT).decode()
    assert state[state.index('namespace BotwCompanion'):]==original[original.index('namespace BotwCompanion'):]
    for source in (MOD/'native').glob('botw_*.inc'):
        baseline=ROOT/'switch/eden-upstream/src/android/app/src/main/jni'/source.name
        assert source.read_text(encoding='utf-8')==baseline.read_text(encoding='utf-8'), source.name
    ui=MOD/'ui/src/main/java/com/dualscreen/mods/botw'
    for source in ui.glob('*.kt'):
        assert 'org.yuzu.yuzu_emu' not in source.read_text(encoding='utf-8'), source.name
    assert 'botw_companion.cpp' not in (ROOT/'switch/eden-upstream/src/android/app/src/main/jni/CMakeLists.txt').read_text()
    assert 'InstallBotwCompanionEquipHook' not in (ROOT/'switch/eden-upstream/src/core/loader/nso.cpp').read_text()
    runtime=(ROOT/'switch/eden-runtime/build.gradle.kts').read_text()
    assert 'exclude("botw/**")' in runtime and 'assets.srcDir(generatedBotwPlayerAssets)' not in runtime
    assert (ROOT/'switch/mods/breath-of-the-wild/manifest.json').read_bytes()==(ROOT/'launcher/src/main/assets/mods/switch/breath-of-the-wild/manifest.json').read_bytes()
    old_api=subprocess.check_output(['git','show','87a35dc:switch/eden-upstream/src/core/companion/module_api.h'],cwd=ROOT).decode()
    new_api=(ROOT/'switch/eden-upstream/src/core/companion/module_api.h').read_text()
    for name,extension in [('DsHostApi','    uint8_t* (*decode_rgba)'),('DsModuleApi','    size_t (*query)')]:
        start='struct '+name+' {'
        old_layout=old_api[old_api.index(start):old_api.index('};',old_api.index(start))]
        new_prefix=new_api[new_api.index(start):new_api.index(extension)]
        assert old_layout==new_prefix,'Legacy Z-A ABI prefix changed: '+name
    print('PASS: unchanged working hooks/state/includes; isolated UI/native/assets; matching manifests')
    print('PASS: existing Z-A native host/module ABI prefixes unchanged')

def asset_contracts(player_assets):
    base=ROOT/'switch/eden-upstream/src/android/app/src/dualscreen/assets/botw'
    entries={p.relative_to(base).as_posix():p for p in base.rglob('*') if p.is_file()}
    for p in player_assets.glob('*.glb'):
        if p.name=='Link.glb' or p.name.startswith('Armor_'): entries['player/'+p.name]=p
    index=json.loads(entries['player/index.json'].read_text(encoding='utf-8'))
    required=0
    for actor,value in index['actors'].items():
        model=value if isinstance(value,str) else value.get('file')
        if model and (actor.startswith(('Armor_','Item_','Animal_Insect_')) or actor=='Link'):
            assert 'player/'+model in entries,(actor,model)
            required+=1
    # Verify parity with the last good APK, without requiring its presence in source control.
    apk=ROOT/'launcher/build/outputs/apk/release/launcher-release.apk'
    if apk.is_file():
        with zipfile.ZipFile(apk) as archive:
            prior=[n for n in archive.namelist() if n.startswith('assets/botw/') and not n.endswith('/')]
            if prior:
                assert set(entries)=={n[len('assets/botw/'):] for n in prior},'BOTW asset inventory changed'
                for name in prior:
                    assert hashlib.sha256(archive.read(name)).digest()==hashlib.sha256(entries[name[len('assets/botw/'):]].read_bytes()).digest(),name
                print(f'PASS: all {len(prior)} previous APK assets are byte-identical package inputs')
    total=sum(p.stat().st_size for p in entries.values())
    assert total<120*1024*1024
    print(f'PASS: {required} indexed armor/held/Link entries; {len(entries)} assets, {total} bytes; cache inputs preserved')

def typecheck(work):
    cache=Path(os.environ['USERPROFILE'])/'.gradle/caches/modules-2/files-2.1'
    java=Path(os.environ.get('JAVA_HOME','C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))/'bin/java.exe'
    def jar(group,name,version):
        matches=list((cache/group/name/version).glob(f'*/{name}-{version}.jar'))
        assert len(matches)==1,(name,matches)
        return str(matches[0])
    std=jar('org.jetbrains.kotlin','kotlin-stdlib','1.9.24')
    annotations=jar('org.jetbrains','annotations','13.0')
    compiler=[jar('org.jetbrains.kotlin','kotlin-compiler-embeddable','1.9.24'),std,annotations,
              jar('org.jetbrains.kotlin','kotlin-reflect','1.6.10'),
              jar('org.jetbrains.kotlin','kotlin-script-runtime','1.9.24'),
              jar('org.jetbrains.intellij.deps','trove4j','1.0.20200330')]
    deps=[std,annotations,str(Path(os.environ['LOCALAPPDATA'])/'Android/Sdk/platforms/android-36/android.jar')]
    for name in ['filament-android','gltfio-android']:
        archives=list((cache/'com.google.android.filament'/name/'1.75.0').glob('*/*.aar'))
        assert len(archives)==1
        target=work/(name+'.jar')
        with zipfile.ZipFile(archives[0]) as z: target.write_bytes(z.read('classes.jar'))
        deps.append(str(target))
    sources=list((MOD/'ui/src/main/java').rglob('*.kt'))
    sources.append(ROOT/'switch/companion-api/src/main/java/com/dualscreen/companion/api/CompanionHost.kt')
    subprocess.run([str(java),'-Xmx512m','-XX:MaxMetaspaceSize=256m','-XX:+UseSerialGC','-XX:ActiveProcessorCount=2',
        '-cp',os.pathsep.join(compiler),'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
        '-no-stdlib','-no-reflect','-jvm-target','17','-classpath',os.pathsep.join(deps),
        '-d',str(work/'classes'),*[str(p) for p in sources]],check=True)
    print('PASS: complete external BOTW UI type-check against actual Android/Filament/host API')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--typecheck',action='store_true');p.add_argument('--player-assets',type=Path);a=p.parse_args()
    contracts()
    if a.player_assets: asset_contracts(a.player_assets)
    if a.typecheck:
        work=ROOT/'.work/botw-package/typecheck';work.mkdir(parents=True,exist_ok=True);typecheck(work)
