"""Stage an explicitly authorized AC II development update for the next launch.

Uses app-private versioned payload storage. Does not touch game files, preferences,
running processes, input, saves, or the APK. The launcher's normal pre-launch
installer deploys the new manifest after the user exits and opens AC II again.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tempfile
import uuid
import zipfile

PACKAGE = 'com.dualscreen.launcher'
ROOT = 'files/game-payloads/assassins-creed-ii'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--descriptor', required=True, type=Path)
    parser.add_argument('--receipt', required=True, type=Path)
    args = parser.parse_args()
    if args.receipt.exists():
        raise ValueError('Choose a new receipt; previous rollback records are preserved')
    descriptor_bytes = args.descriptor.read_bytes()
    descriptor = json.loads(descriptor_bytes)
    name = descriptor['name']
    if not re.fullmatch(r'assassins-creed-ii-[A-Za-z0-9._-]+\.zip', name):
        raise ValueError('Invalid archive name')
    archive = args.descriptor.parent / name
    archive_bytes = archive.read_bytes()
    digest = hashlib.sha256(archive_bytes).hexdigest()
    if descriptor['gameId'] != 'assassins-creed-ii' or descriptor['status'] != 'development' or \
            digest != descriptor['sha256'] or len(archive_bytes) != descriptor['bytes']:
        raise ValueError('Invalid development descriptor')
    files = {}
    with zipfile.ZipFile(archive) as z:
        manifest_bytes = z.read('manifest.json')
        manifest = json.loads(manifest_bytes)
        if manifest['version'] != descriptor['version']:
            raise ValueError('Version mismatch')
        allowed = {'dinput8.dll', 'AssassinsCreedIISecondScreen/COPYING.txt', 'AssassinsCreedIISecondScreen/SOURCE.zip'}
        if len(manifest['files']) != len(allowed):
            raise ValueError('Unexpected AC II payload files')
        for entry in manifest['files']:
            source = entry['source']
            if source not in allowed or source != entry['target'] or source in files:
                raise ValueError('Unexpected payload target')
            value = z.read('payload/' + source)
            if not value or hashlib.sha256(value).hexdigest() != entry['sha256']:
                raise ValueError('Payload digest mismatch')
            files[source] = value
        if set(z.namelist()) != {'manifest.json'} | {'payload/' + n for n in allowed}:
            raise ValueError('Unexpected archive contents')
    files = {'payload/' + n: data for n, data in files.items()}
    files.update({'manifest.json': manifest_bytes, 'descriptor.json': descriptor_bytes})
    adb_prefix = [args.adb, '-s', args.serial]
    def adb(*command):
        return subprocess.run(adb_prefix + list(command), check=True, capture_output=True, timeout=30).stdout
    def app(command):
        return adb('shell', '-T', 'run-as ' + PACKAGE + ' sh -c ' + shlex.quote(command))
    def read(path):
        return adb('exec-out', 'run-as', PACKAGE, 'cat', path)
    previous = read(ROOT + '/current')
    previous_pin = read(ROOT + '/development-package')
    if not re.fullmatch(b'[0-9a-f]{64}', previous) or previous_pin != previous:
        raise ValueError('Expected the existing explicitly pinned AC II development package')
    previous_descriptor = json.loads(read(ROOT + '/' + previous.decode() + '/descriptor.json'))
    if previous_descriptor['gameId'] != 'assassins-creed-ii':
        raise ValueError('Existing cache belongs to another game')
    cache = ROOT + '/' + digest
    token = uuid.uuid4().hex
    transfer = '/data/local/tmp/ac2-payload-' + token
    stage = ROOT + '/.staging-' + token
    receipt = dict(version=descriptor['version'], sha256=digest, previous=previous.decode(),
        previous_version=previous_descriptor['version'], game_files_changed=False, apk_changed=False,
        activated=False, files={n: hashlib.sha256(v).hexdigest() for n, v in files.items()})
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='ac2-stage-', dir=args.receipt.parent) as temporary:
        folder = Path(temporary)
        for path, value in files.items():
            local = folder / path
            local.parent.mkdir(parents=True, exist_ok=True)
            local.write_bytes(value)
        (folder / 'pin').write_bytes(digest.encode())
        adb('push', str(folder), transfer)
        app('mkdir -p ' + shlex.quote(stage))
        for path in files:
            target = stage + '/' + path
            app('mkdir -p ' + shlex.quote(str(PurePosixPath(target).parent)) + ' && cp ' +
                shlex.quote(transfer + '/' + path) + ' ' + shlex.quote(target))
            if read(target) != files[path]:
                raise ValueError('Device cache verification failed: ' + path)
        # Keep all prior versions. Never overwrite a version directory in use.
        exists = app('if test -e ' + shlex.quote(cache) + '; then echo yes; fi').strip()
        if exists:
            for path, value in files.items():
                if read(cache + '/' + path) != value:
                    raise ValueError('Existing hash directory is damaged; refusing to overwrite it')
        else:
            app('mv ' + shlex.quote(stage) + ' ' + shlex.quote(cache))
        pin = ROOT + '/.pin-' + token
        pointer = ROOT + '/.current-' + token
        app('cp ' + shlex.quote(transfer + '/pin') + ' ' + shlex.quote(pin) + ' && cp ' +
            shlex.quote(transfer + '/pin') + ' ' + shlex.quote(pointer))
        # Avoid overwriting a concurrently changed launcher selection. No prefs
        # edit is needed; GameInstaller records the deployed version on launch.
        command = ('test "$(cat ' + shlex.quote(ROOT + '/current') + ')" = ' + shlex.quote(previous.decode()) +
            ' && test "$(cat ' + shlex.quote(ROOT + '/development-package') + ')" = ' + shlex.quote(previous_pin.decode()) +
            ' && mv ' + shlex.quote(pin) + ' ' + shlex.quote(ROOT + '/development-package') +
            ' && mv ' + shlex.quote(pointer) + ' ' + shlex.quote(ROOT + '/current'))
        app(command)
        if read(ROOT + '/current') != digest.encode() or read(ROOT + '/development-package') != digest.encode():
            raise ValueError('Cache selection verification failed')
    receipt['activated'] = True
    receipt['status'] = 'staged in launcher cache; current game still uses its loaded DLL until restart'
    args.receipt.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
