import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('combine_weapon_dds', Path(__file__).parents[1] / 'tools/combine_weapon_dds.py')
dds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dds)


class WeaponDdsTests(unittest.TestCase):
    def fixture(self, directory):
        header = bytearray(128)
        header[:4] = b'DDS '
        for offset, value in ((4, 124), (12, 16), (16, 16), (28, 3), (76, 32)):
            struct.pack_into('<I', header, offset, value)
        header[84:88] = b'DXT5'
        path = Path(directory) / 'weapon.dds.0'
        path.write_bytes(header + b'a' * 16)
        path.with_suffix('.1').write_bytes(b'b' * 64)
        path.with_suffix('.2').write_bytes(b'c' * 256)
        return path

    def test_exact_mip_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(directory)
            original = path.read_bytes()
            result = dds.combine(path)
            self.assertEqual(result[128:], b'c' * 256 + b'b' * 64 + b'a' * 16)
            self.assertEqual(path.read_bytes(), original)

    def test_reject_missing_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(directory)
            path.with_suffix('.1').unlink()
            with self.assertRaises(ValueError):
                dds.combine(path)

    def test_reject_truncated_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(directory)
            path.with_suffix('.2').write_bytes(b'x')
            with self.assertRaises(ValueError):
                dds.combine(path)

    def test_reject_unknown_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(directory)
            data = bytearray(path.read_bytes())
            data[84:88] = b'????'
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                dds.combine(path)

    def test_bc5_exact_mip_order(self):
        with tempfile.TemporaryDirectory() as directory:
            path = self.fixture(directory)
            data = bytearray(path.read_bytes())
            data[84:88] = b'ATI2'
            path.write_bytes(data)
            result = dds.combine(path)
            self.assertEqual(result[84:88], b'ATI2')
            self.assertEqual(result[128:], b'c' * 256 + b'b' * 64 + b'a' * 16)

    def test_base_level_without_mip_flag(self):
        with tempfile.TemporaryDirectory() as directory:
            header=bytearray(128)
            header[:4]=b'DDS '
            for offset,value in ((4,124),(8,0x1007),(12,8),(16,8),(76,32)):
                struct.pack_into('<I',header,offset,value)
            header[84:88]=b'DXT1'
            path=Path(directory)/'grey.dds.0'
            path.write_bytes(header+b'x'*32)
            self.assertEqual(dds.combine(path),path.read_bytes())


if __name__ == '__main__':
    unittest.main()
