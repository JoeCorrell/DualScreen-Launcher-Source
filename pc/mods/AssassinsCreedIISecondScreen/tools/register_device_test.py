"""Register AC II's existing device files in Skyrim's launcher container.

Requires the development APK (run-as), an explicitly requested device test,
and a stopped launcher. Installs a separately packaged, verified local payload;
never substitutes Skyrim files or alters saves. Does not launch either game.
"""
import argparse
import hashlib
import json
import pathlib
import shlex
import subprocess
import xml.etree.ElementTree as ET
import zipfile

PACKAGE = 'com.dualscreen.launcher'
GAME = 'assassins-creed-ii'
EXECUTABLE_HASH = '23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--descriptor', required=True, type=pathlib.Path)
    parser.add_argument('--backup', required=True, type=pathlib.Path)
    args = parser.parse_args()
    def adb(*command, data=None):
        return subprocess.run([args.adb, *command], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
    def shell(command):
        return adb('shell', command).decode().strip()
    def app(command, data=None):
        return adb('shell', '-T', 'run-as ' + PACKAGE + ' sh -c ' + shlex.quote(command), data=data)
    def read(path):
        # exec-out preserves binary bytes; this device's shell service converts LF to CRLF.
        return adb('exec-out', 'run-as', PACKAGE, 'cat', path)
    def write(path, data):
        parts = pathlib.PurePosixPath(path).parts
        if '..' in parts or path.startswith('/') or not path.startswith(('files/', 'shared_prefs/')):
            raise ValueError('Refusing a path outside launcher test storage')
        app('mkdir -p ' + shlex.quote(str(pathlib.PurePosixPath(path).parent)))
        staging = path + '.ac2-staging'
        # ADB's interactive input path can consume control bytes in DLLs.
        # push + run-as cp keeps the entire transfer binary-safe.
        transfer = args.backup.parent / 'device-transfer.bin'
        transfer.write_bytes(data)
        adb('push', str(transfer), '/data/local/tmp/ac2-payload-transfer')
        app('cp /data/local/tmp/ac2-payload-transfer ' + shlex.quote(staging))
        app('mv ' + shlex.quote(staging) + ' ' + shlex.quote(path))
        if read(path) != data:
            raise ValueError('Device verification failed: ' + path)

    if shell('pidof ' + PACKAGE + ' || true'):
        raise ValueError('Stop the launcher before registering its test payload')
    descriptor = json.loads(args.descriptor.read_text())
    archive = args.descriptor.parent / descriptor['name']
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if descriptor['gameId'] != GAME or descriptor['status'] != 'development' or descriptor['sha256'] != digest or archive.stat().st_size != descriptor['bytes']:
        raise ValueError('Invalid development package')
    actual = shell("sha256sum '/sdcard/Games/Assassins Creed II/AssassinsCreedIIGame.exe'").split()[0]
    if actual != EXECUTABLE_HASH:
        raise ValueError('The device executable does not match the native build')
    registry_path = 'shared_prefs/installed_games.xml'
    if app('if test -e shared_prefs/installed_games.xml.bak; then echo pending-backup; fi').strip():
        raise ValueError('An Android preferences backup is pending; reopen and close the launcher first')
    before = read(registry_path)
    args.backup.parent.mkdir(parents=True, exist_ok=True)
    if args.backup.exists():
        raise ValueError('Backup already exists; choose a new checkpoint path')
    args.backup.write_bytes(before)
    registry = ET.fromstring(before)
    skyrim = registry.find("./string[@name='skyrim-se']")
    if skyrim is None or not skyrim.text:
        raise ValueError('Skyrim is not registered in this launcher')
    container = json.loads(skyrim.text)['containerId']
    if not isinstance(container, int) or container <= 0:
        raise ValueError('Invalid Skyrim container')
    prefix = f'files/imagefs/home/xuser-{container}'
    config = json.loads(read(prefix + '/.container'))
    if config.get('name') != 'Games':
        raise ValueError('Skyrim is not using the expected Games container')
    if registry.find("./string[@name='assassins-creed-ii']") is not None:
        raise ValueError('AC II is already registered; use a payload update instead')
    cache = 'files/game-payloads/' + GAME + '/' + digest
    with zipfile.ZipFile(archive) as package:
        if set(package.namelist()) != {'manifest.json', 'payload/dinput8.dll'}:
            raise ValueError('Unexpected development package contents')
        manifest_bytes = package.read('manifest.json')
        manifest = json.loads(manifest_bytes)
        dll = package.read('payload/dinput8.dll')
        dll_digest = hashlib.sha256(dll).hexdigest()
        if manifest['version'] != descriptor['version'] or manifest['files'] != [dict(source='dinput8.dll', target='dinput8.dll', sha256=dll_digest)]:
            raise ValueError('Invalid native payload manifest')
        # DLL installation is separate from APK installation, verified on both sides.
        dll_file = args.backup.parent / 'dinput8-device-test.dll'
        dll_file.write_bytes(dll)
        existing = shell("if test -f '/sdcard/Games/Assassins Creed II/dinput8.dll'; then sha256sum '/sdcard/Games/Assassins Creed II/dinput8.dll'; fi")
        if existing and existing.split()[0] != dll_digest:
            raise ValueError('A different dinput8.dll is installed; preserve it before registering this test')
        adb('push', str(dll_file), '/sdcard/Games/Assassins Creed II/dinput8.dll')
        if shell("sha256sum '/sdcard/Games/Assassins Creed II/dinput8.dll'").split()[0] != dll_digest:
            raise ValueError('Installed native DLL failed verification')
        write(cache + '/manifest.json', manifest_bytes)
        write(cache + '/payload/dinput8.dll', dll)
    write(cache + '/descriptor.json', args.descriptor.read_bytes())
    write('files/game-payloads/' + GAME + '/development-package', digest.encode())
    write('files/game-payloads/' + GAME + '/current', digest.encode())
    relative_shortcut = prefix + '/.wine/drive_c/users/xuser/Desktop/Assassins Creed II.desktop'
    shortcut = '[Desktop Entry]\nName=Assassins Creed II\nExec=F:\\Games\\Assassins Creed II\\AssassinsCreedIIGame.exe\nType=Application\nStartupNotify=True\n[Extra Data]\nenvVars=WINEDLLOVERRIDES=icu=n;mscoree=d;mshtml=d\n'
    write(relative_shortcut, shortcut.encode())
    entry = ET.SubElement(registry, 'string', name=GAME)
    installed_bytes = int(shell("du -sb '/sdcard/Games/Assassins Creed II'").split()[0])
    entry.text = json.dumps(dict(containerId=container, shortcutPath='/data/user/0/' + PACKAGE + '/' + relative_shortcut,
        hostDirectory='/storage/emulated/0/Games/Assassins Creed II', installedBytes=installed_bytes,
        payloadVersion=descriptor['version'], untestedBuild=False,
        compatibilityWarning='Development payload: live integration is incomplete.', skippedPayloadTargets=[]))
    write(registry_path, ET.tostring(registry, encoding='utf-8', xml_declaration=True))
    print(f'Registered Assassin\'s Creed II in Skyrim container {container}; payload {descriptor["version"]}. Game not launched.')


if __name__ == '__main__':
    main()
