import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ac2_package', ROOT / 'tools/package_payload.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageTests(unittest.TestCase):
    def test_reproducible_complete_source_and_manifest(self):
        with tempfile.TemporaryDirectory(prefix='ac2-package-test-', dir=ROOT / 'build') as temporary:
            folder = Path(temporary)
            dll = folder / 'fixture.dll'
            dll.write_bytes(b'MZ' + bytes(4094))
            archive = module.package(dll, folder / 'out', 'test.1')
            first = archive.read_bytes()
            self.assertEqual(first, module.package(dll, folder / 'out', 'test.1').read_bytes())
            descriptor = json.loads((archive.parent / 'assassins-creed-ii-payload.json').read_text())
            self.assertEqual(descriptor['status'], 'development')
            self.assertEqual(descriptor['sha256'], hashlib.sha256(first).hexdigest())
            with zipfile.ZipFile(io.BytesIO(first)) as package:
                manifest = json.loads(package.read('manifest.json'))
                self.assertEqual(len(package.namelist()), len(manifest['files']) + 1)
                for file in manifest['files']:
                    self.assertEqual(file['sha256'], hashlib.sha256(package.read('payload/' + file['source'])).hexdigest())
                    self.assertEqual(file['source'], file['target'])
                with zipfile.ZipFile(io.BytesIO(package.read('payload/AssassinsCreedIISecondScreen/SOURCE.zip'))) as source:
                    names = set(source.namelist())
                    self.assertTrue({'src/Runtime.cpp', 'src/NativeMapMarkers.h', 'src/KnownCityMaps.h',
                                     'build.ps1', 'COPYING', 'third_party/lzo/src/lzo2a_d2.c'} <= names)
                    self.assertFalse(any(Path(name).suffix.lower() in ('.exe', '.dll', '.forge', '.png', '.bin', '.sav') for name in names))
                    self.assertFalse(any(part in ('.work', '.research', 'build') for name in names for part in Path(name).parts))
            with self.assertRaises(ValueError):
                module.package(dll, folder / 'out', '../escape')
            dll.write_bytes(b'not a DLL')
            with self.assertRaises(ValueError):
                module.package(dll, folder / 'out', 'test.2')


if __name__ == '__main__':
    unittest.main()
