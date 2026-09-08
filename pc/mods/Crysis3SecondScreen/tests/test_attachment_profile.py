import hashlib
from pathlib import Path
import re
import struct
import unittest

PROJECT = Path(__file__).resolve().parents[1]


class AttachmentProfileTests(unittest.TestCase):
    def test_exact_captured_read_contract(self):
        capture = PROJECT.parents[2] / '.work/crysis3/live-190400/capture'
        if not capture.is_dir():
            self.skipTest('Private exact-image capture required')
        text = (capture / 'loaded-text.bin').read_bytes()
        rdata = (capture / 'loaded-rdata.bin').read_bytes()
        source = (PROJECT / 'src/NativeAttachmentRead.h').read_text()
        entries = re.findall(r'\{(0x[0-9a-f]+),(0x[0-9a-f]+|\d+),"([0-9a-f]{64})"\}', source)
        self.assertEqual(len(entries), 4)
        for address, size, expected in entries:
            address, size = int(address, 0), int(size, 0)
            data, base = (text, 0x37001000) if address < 0x3987f000 else (rdata, 0x3987f000)
            actual = data[address-base:address-base+size]
            self.assertEqual(len(actual), size)
            self.assertEqual(hashlib.sha256(actual).hexdigest(), expected)
        for offset, target in ((0x230, 0x38225a30), (0x234, 0x382259e0), (0x240, 0x38226380)):
            self.assertEqual(struct.unpack_from('<I', rdata, 0x399e3d08-0x3987f000+offset)[0], target)
