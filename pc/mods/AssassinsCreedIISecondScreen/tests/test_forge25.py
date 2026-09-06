import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest

source = Path(__file__).resolve().parents[1] / 'tools' / 'forge25.py'
spec = importlib.util.spec_from_file_location('forge25', source)
forge25 = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = forge25
spec.loader.exec_module(forge25)


class ForgeBoundsTests(unittest.TestCase):
    def test_resource_index_preserves_identity_and_checks_total(self):
        resource = struct.pack('<III', 123, 4, 4) + b'Test\0' + b'body'
        index = struct.pack('<HIIH', 1, 55, len(resource), 65535) + bytes(4)
        self.assertEqual(forge25.resource_index(index, resource)[0]['name'], 'Test')
        for bad_index, bad_data in [(index[:-1], resource), (index, resource[:-1]),
                                    (index, resource + b'x'), (index, b'\0' * 8 + b'\xff' * 4 + resource[12:])]:
            with self.assertRaises(forge25.FormatError):
                forge25.resource_index(bad_index, bad_data)

    def test_file_ranges_and_wrong_version_fail_closed(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'test.forge'
            path.write_bytes(b'scimitar\0' + struct.pack('<IQ', 27, 100))
            archive = forge25.Forge25(path)
            with self.assertRaises(forge25.FormatError): archive.entries()
            for start, size in [(-1, 1), (0, -1), (20, 2), (0, forge25.MAX_EXPANDED + 1)]:
                with self.assertRaises(forge25.FormatError): archive.read(start, size)

    def test_compressed_block_extent_is_verified_before_decompression(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / 'test.forge'
            data = forge25.MAGIC + bytes(2) + b'\x02' + bytes(3) + b'\x80' + struct.pack('<H', 1)
            data += struct.pack('<HH', 100, 20) + bytes(4) + b'short'
            header = bytearray(444); header[:8] = b'FILEDATA'; struct.pack_into('<I', header, 395, len(data))
            path.write_bytes(header + data)
            with self.assertRaises(forge25.FormatError):
                forge25.Forge25(path).unpack(forge25.Entry(1, 0, len(data), 'Test'))


if __name__ == '__main__': unittest.main()
