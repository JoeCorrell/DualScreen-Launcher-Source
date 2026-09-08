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
import io

GAME = 'assassins-creed-ii'
RELEASE = 'https://github.com/JoeCorrell/DualScreen-Launcher-Payloads/releases/download/launcher-payloads-v1'


def write_entry(zip_, name: str, value: bytes):
    info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    zip_.writestr(info, value)


def build_source() -> bytes:
    root = Path(__file__).resolve().parents[1]
    selected = [root / name for name in ('build.ps1', 'README.md', 'COPYING', 'tools/package_payload.py',
        'tools/patch_landing_contacts.py', 'tools/landing_contact_search.S',
        'tools/patch_landing_retry.py', 'tools/landing_retry.S', 'tools/landing_retry_code.py',
        'tools/patch_landing_order.py', 'tools/landing_order.S', 'tools/landing_order_code.py',
        'tools/LANDING_ORDER_PATCH.md',
        'tools/LandingContactInspect.h')]
    for directory in ('src', 'tests', 'third_party'):
        for path in sorted((root / directory).rglob('*')):
            if path.is_file() and (path.suffix in ('.h', '.cpp', '.c', '.ch', '.def', '.md', '.py') or path.name == 'COPYING'):
                selected.append(path)
    result = io.BytesIO()
    with zipfile.ZipFile(result, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as source:
        for path in sorted(selected):
            if path.is_symlink() or not path.resolve().is_relative_to(root):
                raise ValueError('Build source escapes native project')
            write_entry(source, path.relative_to(root).as_posix(), path.read_bytes())
    return result.getvalue()


def package(dll: Path, output: Path, version: str) -> Path:
    import re
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,95}', version):
        raise ValueError('Invalid version')
    data = dll.read_bytes()
    if data[:2] != b'MZ' or len(data) < 4096:
        raise ValueError('Expected a compiled native DLL')
    root = Path(__file__).resolve().parents[1]
    files = {'dinput8.dll': data,
        'AssassinsCreedIISecondScreen/COPYING.txt': (root / 'COPYING').read_bytes(),
        'AssassinsCreedIISecondScreen/SOURCE.zip': build_source()}
    manifest = dict(version=version, files=[dict(source=name, target=name,
        sha256=hashlib.sha256(value).hexdigest()) for name, value in files.items()])
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'{GAME}-{version}.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zip_:
        write_entry(zip_, 'manifest.json', (json.dumps(manifest, indent=2) + '\n').encode())
        for name, value in files.items():
            write_entry(zip_, 'payload/' + name, value)
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
