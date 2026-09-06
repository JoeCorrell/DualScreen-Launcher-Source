"""Ensure externalizing Z-A actually removes its executable/UI/assets from the APK."""
import argparse, hashlib, json, zipfile
from pathlib import Path

ap=argparse.ArgumentParser();ap.add_argument('apk', type=Path);args=ap.parse_args()
with zipfile.ZipFile(args.apk) as apk:
    names=apk.namelist()
    assert not any(n.startswith('assets/plza/') or n.endswith('.dsmod') for n in names)
    for name in names:
        if name.endswith('.dex'):
            dex=apk.read(name)
            assert b'Lorg/yuzu/yuzu_emu/activities/PlzaCompanionView;' not in dex
            assert b'Lcom/dualscreen/mods/plza/PlzaCompanionView;' not in dex
    native=apk.read('lib/arm64-v8a/libyuzu-android.so')
    assert b'[PLZA-ITEM-ACTION]' not in native, 'Native game implementation still embedded'
    assert b'[Companion] external module' in native, 'Generic native loader missing'
    assert b'Java_org_yuzu_yuzu_1emu_NativeLibrary_loadExternalCompanion' in native
    manifest=json.loads(apk.read('assets/mods/switch/pokemon-legends-za/manifest.json'))
    assert manifest['delivery']=='external-module'
    assert any(n.startswith('assets/botw/player/') for n in names)
    assert any(n.startswith('assets/smo/player/') for n in names)
    # Preserve every pre-existing dual-screen asset, not just generated player GLBs.
    source=Path(__file__).resolve().parents[3] / 'eden-upstream/src/android/app/src/dualscreen/assets'
    for entry in source.rglob('*'):
        if entry.is_file():
            relative=entry.relative_to(source).as_posix()
            if not relative.startswith('plza/'):
                assert 'assets/'+relative in names, f'Missing other-game asset: {relative}'
    print(json.dumps({'apk_sha256':hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        'native_sha256':hashlib.sha256(native).hexdigest(),'bytes':args.apk.stat().st_size,
        'plza_embedded_ui_assets_payload':False,'other_player_assets_preserved':True}))
