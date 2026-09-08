"""Manifest regression; private capture checks skip only when evidence is absent."""
import hashlib
from pathlib import Path
import re
import unittest

PROJECT = Path(__file__).resolve().parents[1]
CAPTURE = PROJECT.parents[2] / '.work/crysis3/live-190400/capture'


def entries(name='NativeWeaponProfile'):
    body = (PROJECT / 'src/NativeWeaponProfile.h').read_text().split(name + '[] = {', 1)[1].split('};', 1)[0]
    return [(int(a, 0), int(n, 0), h) for a, n, h in re.findall(
        r'\{(0x[0-9a-f]+),(0x[0-9a-f]+|[0-9]+),"([0-9a-f]{64})"\}',
        body)]


class NativeWeaponProfileTests(unittest.TestCase):
    def test_manifest_is_bounded_and_contains_core_contracts(self):
        values = entries()
        self.assertEqual(len(values), 33)
        self.assertEqual(len({a for a, _, _ in values}), len(values))
        required = {0x37283230, 0x37279e30, 0x382532b0, 0x38278940,
                    0x38250890, 0x3821e8b0, 0x378a6cb0, 0x378b9070,
                    0x399e6b28, 0x399e3b70, 0x3988c288}
        self.assertTrue(required <= {a for a, _, _ in values})
        for address, size, _ in values:
            self.assertTrue(0 < size <= 4096)
            self.assertTrue(0x37001000 <= address < 0x39b40000)

    def test_every_fingerprint_matches_integrity_checked_capture(self):
        if not CAPTURE.is_dir():
            self.skipTest('Private runtime capture is not distributed with source')
        text = (CAPTURE / 'loaded-text.bin').read_bytes()
        rdata = (CAPTURE / 'loaded-rdata.bin').read_bytes()
        self.assertEqual(hashlib.sha256(text).hexdigest(),
                         'e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f')
        self.assertEqual(hashlib.sha256(rdata).hexdigest(),
                         '376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe')
        for address, size, expected in entries() + entries('NativeWeaponNameProfile'):
            with self.subTest(address=hex(address)):
                base, data = ((0x37001000, text) if address < 0x3987f000
                              else (0x3987f000, rdata))
                offset = address - base
                self.assertTrue(0 <= offset <= len(data) - size)
                self.assertEqual(hashlib.sha256(data[offset:offset + size]).hexdigest(), expected)

    def test_name_families_share_exact_common_contract(self):
        values = entries('NativeWeaponNameProfile')
        self.assertEqual(len(values), 27)
        self.assertEqual(len({a for a, _, _ in values}), 27)
        self.assertEqual([a for a, _, _ in values[:2]], [0x3821bac0, 0x3821bb70])
        if not CAPTURE.is_dir():
            self.skipTest('Private runtime capture is not distributed with source')
        rdata = (CAPTURE / 'loaded-rdata.bin').read_bytes()
        for address, size, _ in values[2:]:
            self.assertEqual(size, 0x12c)
            for offset, target in [(0x18, 0x3821bac0), (0x64, 0x3821bb70), (0x128, 0x37f6c960)]:
                start = address - 0x3987f000 + offset
                self.assertEqual(int.from_bytes(rdata[start:start+4], 'little'), target)


if __name__ == '__main__':
    unittest.main()
