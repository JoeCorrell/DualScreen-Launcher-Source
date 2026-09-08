"""Build local release-signed instrumentation for ONE verified development payload.

Does not change/build the launcher, publish the payload, or access a device.
The resulting maintenance APK must not run while any launcher game is active.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[4]
    descriptor_file = args.package / 'dishonored-payload.json'
    descriptor = json.loads(descriptor_file.read_text(encoding='utf-8'))
    assert descriptor['status'] == 'development' and descriptor['gameId'] == 'dishonored'
    assert Path(descriptor['name']).name == descriptor['name']
    archive = args.package / descriptor['name']
    assert archive.stat().st_size == descriptor['bytes']
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == descriptor['sha256']
    with zipfile.ZipFile(archive) as check:
        assert check.testzip() is None
    sdk = Path(os.environ.get('ANDROID_HOME', Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'))
    java = Path(os.environ.get('JAVA_HOME', 'C:/Program Files/Microsoft/jdk-17.0.19.10-hotspot'))
    build = sdk / 'build-tools/35.0.0'
    android = sdk / 'platforms/android-35/android.jar'
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    assets = output / 'assets'; assets.mkdir(exist_ok=True)
    classes = output / 'classes'; classes.mkdir(exist_ok=True)
    dex = output / 'dex'; dex.mkdir(exist_ok=True)
    shutil.copyfile(archive, assets / 'payload.zip')
    shutil.copyfile(descriptor_file, assets / 'descriptor.json')
    manifest = output / 'AndroidManifest.xml'
    manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android"
        package="com.dualscreen.dishonoredinstaller" android:versionCode="1" android:versionName="1">
        <uses-sdk android:minSdkVersion="26" android:targetSdkVersion="35"/>
        <application android:label="Dishonored development installer" android:hasCode="true" android:debuggable="false"/>
        <instrumentation android:name="com.dualscreen.dishonoredinstaller.PayloadInstaller"
            android:targetPackage="com.dualscreen.launcher" android:functionalTest="true"/>
    </manifest>''', encoding='utf-8')
    def run(command, env=None):
        result = subprocess.run([str(x) for x in command], capture_output=True, env=env)
        if result.returncode:
            raise RuntimeError(result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
        return result.stdout.decode(errors='replace')
    run([java / 'bin/javac.exe', '-encoding', 'UTF-8', '-source', '8', '-target', '8', '-classpath', android,
         '-d', classes, *sorted((Path(__file__).parent / 'installer').glob('*.java'))])
    run([java / 'bin/java.exe', '-cp', build / 'lib/d8.jar', 'com.android.tools.r8.D8', '--lib', android,
         '--min-api', '26', '--output', dex, *classes.rglob('*.class')])
    unsigned = output / 'unsigned.apk'
    run([build / 'aapt.exe', 'package', '-f', '-M', manifest, '-I', android, '-A', assets, '-F', unsigned])
    with zipfile.ZipFile(unsigned, 'a', zipfile.ZIP_DEFLATED) as target:
        target.write(dex / 'classes.dex', 'classes.dex')
    aligned = output / 'aligned.apk'
    run([build / 'zipalign.exe', '-f', '4', unsigned, aligned])
    # Never print signing properties or place passwords on command lines.
    properties = {}
    for line in (project / 'keystore.properties').read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith(('#', '!')): continue
        key, value = line.split('=', 1)
        properties[key.strip()] = value.strip().replace('\\:', ':').replace('\\\\', '\\')
    env = os.environ.copy()
    env['DISHONORED_INSTALL_STORE_PASSWORD'] = properties['storePassword']
    env['DISHONORED_INSTALL_KEY_PASSWORD'] = properties['keyPassword']
    signed = output / 'dishonored-local-installer.apk'
    signer = [java / 'bin/java.exe', '-jar', build / 'lib/apksigner.jar']
    run([*signer, 'sign', '--ks', project / properties['storeFile'], '--ks-key-alias', properties['keyAlias'],
         '--ks-pass', 'env:DISHONORED_INSTALL_STORE_PASSWORD', '--key-pass', 'env:DISHONORED_INSTALL_KEY_PASSWORD',
         '--out', signed, aligned], env)
    certificate = run([*signer, 'verify', '--print-certs', signed])
    expected_cert = '21cca830bc0bc8beb13f0e689279f5a5999772b424ea2ef7dbca38582149502d'
    assert 'certificate SHA-256 digest: ' + expected_cert in certificate
    (output / 'receipt.json').write_text(json.dumps({
        'payloadSha256': descriptor['sha256'], 'version': descriptor['version'],
        'apkSha256': hashlib.sha256(signed.read_bytes()).hexdigest(),
        'certificateSha256': expected_cert, 'deviceChanged': False,
    }, indent=2), encoding='utf-8')
    print('Verified local signed installer:', signed)
    print('No launcher rebuild, publication, device installation or game control performed.')


if __name__ == '__main__':
    main()
