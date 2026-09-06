"""Verify an Odyssey development APK without blessing it as gameplay-complete."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import zipfile


def digest(stream):
    result = hashlib.sha256()
    while block := stream.read(1024 * 1024):
        result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apk', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--preview', action='store_true', help='Require self-contained animated Mario preview assets')
    args = parser.parse_args()
    preserved = ('assets/botw/', 'assets/sshd/', 'assets/plza/',
                 'assets/mods/transformers-wfc/')
    with zipfile.ZipFile(args.apk) as new, zipfile.ZipFile(args.baseline) as old:
        assert new.testzip() is None, 'APK ZIP integrity failure'
        checked = 0
        for name in old.namelist():
            if name.endswith('/') or not name.startswith(preserved):
                continue
            with new.open(name) as current, old.open(name) as previous:
                assert digest(current) == digest(previous), f'Existing mod asset changed: {name}'
            checked += 1
        assert checked > 1000, 'Published asset comparison unexpectedly incomplete'
        with new.open('lib/arm64-v8a/libyuzu-android.so') as packed, args.native.open('rb') as source:
            assert digest(packed) == digest(source), 'Wrong native runtime packaged'
        manifest = json.loads(new.read('assets/mods/switch/super-mario-odyssey/manifest.json'))
        assert manifest['titleId'] == '0100000000010000'
        assert manifest['minimumLauncherVersion'] == args.version
        assert manifest['files'] == [], 'Unexpected guest payload installed'
        if args.preview:
            index = json.loads(new.read('assets/smo/player/index.json'))
            assert index['version'] == 2, 'Incomplete legacy preview pack'
            catalog = index['catalog']
            assert len(catalog['bodies']) == 42 and len(catalog['heads']) == 41
            assert 'Mario' in catalog['bodies'] and 'MarioCaptain' in catalog['heads']
            required = set()
            for name in catalog['bodies']:
                for hat in catalog['heads']:
                    body = index['appearances'][name][hat]
                    required.add(body['model'])
                    for part in body['parts']:
                        required.add(part['model'])
                        assert part['joint'] is None or part['joint'].startswith('Armature_'), f'Invalid native attachment: {name}'
            for actor in required:
                raw = new.read(f'assets/smo/player/{actor}.glb')
                assert struct.unpack_from('<III', raw) == (0x46546c67, 2, len(raw))
                size, kind = struct.unpack_from('<II', raw, 12)
                assert kind == 0x4e4f534a
                model = json.loads(raw[20:20+size])
                assert model.get('animations') or model['extras']['smoPreview']['static'], f'No native pose: {actor}'
                assert all('bufferView' in image and 'uri' not in image for image in model.get('images', [])), f'External textures: {actor}'
            print(f'PASS: {len(required)} self-contained native Mario parts packaged')
    metadata = json.loads((args.apk.parent / 'output-metadata.json').read_text())
    assert metadata['elements'][0]['versionName'] == args.version
    with args.apk.open('rb') as stream:
        print('APK SHA256:', digest(stream))
    print(f'PASS: {checked} published assets unchanged; exact rebuilt native; version {args.version}')
    print('This verifies packaging, not on-device functionality or completion.')


if __name__ == '__main__':
    main()
