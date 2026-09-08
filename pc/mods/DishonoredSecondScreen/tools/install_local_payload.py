"""Install a locally signed development payload without replacing the launcher.

Refuses active launcher/WEMU game sessions. No game launches or input injection.
Requires the local installer produced by build_local_installer.py.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import time
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('installer', type=Path)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--dual-screen', action='store_true', help='Restore both displays and remove the baseline override')
    args = parser.parse_args()
    sdk = Path(os.environ.get('ANDROID_HOME', Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'))
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    def run(*command, timeout=60):
        result = subprocess.run(adb + list(command), capture_output=True, timeout=timeout)
        if result.returncode: raise RuntimeError((result.stdout + result.stderr).decode(errors='replace'))
        return result.stdout.decode(errors='replace')
    def shell(*command): return run('shell', shlex.join(command))
    def idle():
        processes = shell('ps', '-A', '-o', 'USER,PID,NAME,ARGS')
        if re.search(r'(?im)^.*(?:wineserver|Dishonored\.exe|wine64-preloader|wine-preloader).*$', processes):
            raise RuntimeError('A Wine game/container is active; no installation performed')
        activities = shell('dumpsys', 'activity', 'activities')
        activities = '\n'.join(line for line in activities.splitlines() if 'mLastPausedActivity:' not in line and 't-1' not in line)
        if re.search(r'(?:EmulationActivity|GameSessionActivity)', activities):
            raise RuntimeError('A launcher emulation activity is present; close it before installing')
    descriptor = json.loads((args.package / 'dishonored-payload.json').read_text(encoding='utf-8'))
    receipt = json.loads((args.installer / 'receipt.json').read_text(encoding='utf-8'))
    archive = args.package / descriptor['name']
    assert archive.resolve().parent == args.package.resolve()
    assert descriptor['sha256'] == receipt['payloadSha256'] == hashlib.sha256(archive.read_bytes()).hexdigest()
    apk = args.installer / 'dishonored-local-installer.apk'
    assert hashlib.sha256(apk.read_bytes()).hexdigest() == receipt['apkSha256']
    binary_root = '/sdcard/Games/Dishonored/Binaries/Win32'
    def remote_hash(path):
        output = shell('sh', '-c', 'if [ -f "$1" ]; then sha256sum "$1"; fi', 'sh', path).split()
        return output[0] if output else None
    assert remote_hash(binary_root + '/Dishonored.exe') == '67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5'
    idle()
    launcher_path = shell('pm', 'path', 'com.dualscreen.launcher').strip().splitlines()[0].removeprefix('package:')
    launcher_hash = remote_hash(launcher_path)
    helper = 'com.dualscreen.dishonoredinstaller'
    installed = False
    try:
        result = run('install', '-r', str(apk), timeout=180)
        assert 'Success' in result, result
        installed = True
        idle()
        # Android checks matching signing certificates before allowing this
        # instrumentation to access the non-debuggable target's private files.
        result = shell('am', 'instrument', '-w', '-e', 'sha256', descriptor['sha256'], '-e', 'mode', 'install',
                       helper + '/com.dualscreen.dishonoredinstaller.PayloadInstaller')
        (args.installer / 'instrumentation-result.txt').write_text(result, encoding='utf-8')
        assert 'status=installed-cache' in result and 'INSTRUMENTATION_CODE: -1' in result, result
        if args.dual_screen:
            idle()
            display_result = shell('am', 'instrument', '-w', '-e', 'mode', 'display-dual',
                helper + '/com.dualscreen.dishonoredinstaller.PayloadInstaller')
            (args.installer / 'display-result.txt').write_text(display_result, encoding='utf-8')
            assert 'status=dual-display-configured' in display_result and 'INSTRUMENTATION_CODE: -1' in display_result, display_result
        idle()
        backup = '/sdcard/Games/Dishonored/.dual-screen-backups/' + time.strftime('%Y%m%d-%H%M%S')
        shell('mkdir', '-p', backup)
        changed = []
        with zipfile.ZipFile(archive) as package:
            manifest = json.loads(package.read('manifest.json'))
            for item in manifest['files']:
                source = item['source']
                assert item['target'] == 'Binaries/Win32/' + source
                assert not any(part in ('', '.', '..') for part in source.split('/')) and '\\' not in source and ':' not in source
                target = binary_root + '/' + source
                previous_hash = remote_hash(target)
                if previous_hash == item['sha256']: continue
                content = package.read('payload/' + source)
                assert hashlib.sha256(content).hexdigest() == item['sha256']
                parent = source.rsplit('/', 1)[0] if '/' in source else ''
                if parent: shell('mkdir', '-p', backup + '/' + parent)
                if previous_hash is not None: shell('cp', target, backup + '/' + source)
                local = args.installer / 'transfer.bin'; local.write_bytes(content)
                run('push', str(local), target + '.pending')
                assert remote_hash(target + '.pending') == item['sha256']
                shell('mv', target + '.pending', target)
                changed.append(source)
            for item in manifest['files']: assert remote_hash(binary_root + '/' + item['source']) == item['sha256']
        assert remote_hash(launcher_path) == launcher_hash
        (args.installer / 'installation.json').write_text(json.dumps({
            'version': descriptor['version'], 'archiveSha256': descriptor['sha256'], 'changedGameFiles': changed,
            'verifiedFiles': len(manifest['files']), 'backup': backup, 'launcherApkPreserved': True,
            'gameLaunched': False, 'deviceInputSent': False, 'runtimeAcceptance': 'Pending gameplay',
        }, indent=2), encoding='utf-8')
        print('Installed and verified', descriptor['version'], '; launcher APK retained; no game launched.')
    finally:
        if installed:
            result = run('uninstall', helper)
            assert 'Success' in result, result


if __name__ == '__main__': main()
