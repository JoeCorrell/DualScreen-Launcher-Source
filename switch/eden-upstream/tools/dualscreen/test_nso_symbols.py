"""Synthetic tests only: no copyrighted executable required."""
import hashlib
import struct
import unittest

from export_nso_symbols import symbols, verify_image


class NsoSymbolTests(unittest.TestCase):
    def fixture(self):
        data = bytearray(512)
        struct.pack_into('<I', data, 4, 32)
        data[32:36] = b'MOD0'
        struct.pack_into('<i', data, 36, 32)
        for i, entry in enumerate(((6, 256), (5, 304), (10, 32), (11, 24), (4, 200), (0, 0))):
            struct.pack_into('<qQ', data, 64 + i * 16, *entry)
        struct.pack_into('<II', data, 200, 1, 2)
        struct.pack_into('<IBBHQQ', data, 280, 1, 2, 0, 1, 400, 8)
        data[305:310] = b'test\0'
        nso = bytearray(256)
        nso[:4] = b'NSO0'
        struct.pack_into('<I', nso, 12, 0x38)
        for i, (location, size) in enumerate(((0, 256), (256, 128), (384, 128))):
            struct.pack_into('<III', nso, 16 + i * 16, 256, location, size)
            nso[160 + i * 32:192 + i * 32] = hashlib.sha256(data[location:location + size]).digest()
        return data, nso

    def test_valid_symbol_and_image(self):
        data, nso = self.fixture()
        verify_image(data, nso)
        self.assertEqual(symbols(data), [dict(name='test', rva=400, size=8, section=1, type=2)])

    def test_wrong_image(self):
        data, nso = self.fixture()
        data[400] ^= 1
        with self.assertRaisesRegex(ValueError, 'does not match'):
            verify_image(data, nso)

    def test_unhashed_segment(self):
        data, nso = self.fixture()
        struct.pack_into('<I', nso, 12, 0)
        with self.assertRaisesRegex(ValueError, 'lacks'):
            verify_image(data, nso)

    def test_trailing_or_truncated_image(self):
        data, nso = self.fixture()
        for malformed in (data[:-1], data + b'\0'):
            with self.assertRaises(ValueError):
                verify_image(malformed, nso)

    def test_outside_name(self):
        data, _ = self.fixture()
        struct.pack_into('<I', data, 280, 32)
        with self.assertRaisesRegex(ValueError, 'outside'):
            symbols(data)

    def test_missing_tags(self):
        data, _ = self.fixture()
        struct.pack_into('<q', data, 64, 0)
        with self.assertRaisesRegex(ValueError, 'Missing'):
            symbols(data)


if __name__ == '__main__':
    unittest.main()
