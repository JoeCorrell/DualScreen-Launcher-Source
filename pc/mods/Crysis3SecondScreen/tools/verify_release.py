"""Read-only package audit: exact Crysis payload and unchanged other game assets."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile


def audit(previous, candidate):
    with zipfile.ZipFile(previous) as old, zipfile.ZipFile(candidate) as new:
        manifest = json.loads(new.read('assets/mods/crysis3/manifest.json'))
        for section in ('files', 'patches'):
            for entry in manifest.get(section, []):
                name = 'assets/mods/crysis3/payload/' + entry['source']
                digest = hashlib.sha256(new.read(name)).hexdigest()
                if digest != entry['sha256']:
                    raise ValueError(f'Payload mismatch: {name}')
        def assets(archive):
            return {entry.filename: hashlib.sha256(archive.read(entry)).hexdigest()
                    for entry in archive.infolist()
                    if not entry.is_dir() and entry.filename.startswith('assets/')
                    and not entry.filename.startswith(('assets/mods/crysis3/', 'assets/dexopt/'))}
        before, after = assets(old), assets(new)
        if before != after:
            changed = sorted(key for key in set(before) | set(after) if before.get(key) != after.get(key))
            raise ValueError(f'Unrelated assets changed: {changed}')
        return {'candidate': str(candidate), 'sha256': hashlib.sha256(Path(candidate).read_bytes()).hexdigest(),
                'crysisPayloadVersion': manifest['version'], 'verifiedFiles': len(manifest.get('files', [])),
                'verifiedPatches': len(manifest.get('patches', [])), 'unchangedOtherAssets': len(before)}


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('verify_release.py PREVIOUS_APK CANDIDATE_APK')
    print(json.dumps(audit(*map(Path, sys.argv[1:])), indent=2))
