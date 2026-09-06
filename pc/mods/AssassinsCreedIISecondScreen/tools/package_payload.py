"""Build a reproducible, independently downloadable AC II development package.

The descriptor deliberately says development. The launcher refuses to activate
it as a release until native integration and device validation are complete.
No Ubisoft assets, extracted data, executables or saves are redistributed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

GAME = 'assassins-creed-ii'
RELEASE = 'https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1'


def package(dll: Path, output: Path, version: str) -> Path:
    import re
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}', version):
        raise ValueError('Invalid version')
    data = dll.read_bytes()
    if data[:2] != b'MZ' or len(data) < 4096:
        raise ValueError('Expected a compiled native DLL')
    manifest = dict(version=version, files=[dict(source='dinput8.dll', target='dinput8.dll',
        sha256=hashlib.sha256(data).hexdigest())])
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'{GAME}-{version}.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zip_:
        for name, value in [('manifest.json', (json.dumps(manifest, indent=2) + '\n').encode()), ('payload/dinput8.dll', data)]:
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            zip_.writestr(info, value)
    descriptor = dict(schema=1, launcherApi=1, gameId=GAME, version=version, status='development',
        name=archive.name, bytes=archive.stat().st_size,
        sha256=hashlib.sha256(archive.read_bytes()).hexdigest(), url=f'{RELEASE}/{archive.name}')
    (output / f'{GAME}-payload.json').write_text(json.dumps(descriptor, indent=2) + '\n', encoding='utf-8')
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--version', default='2026.09.05-dev.1')
    args = parser.parse_args()
    print(package(args.dll, args.output, args.version))
