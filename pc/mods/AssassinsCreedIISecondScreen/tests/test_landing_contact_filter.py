"""Cross-check diagnostic interpretation against the owned game's instructions.

No game is started or modified. This does not reproduce FEX or a live lock.
"""
import os
import random
import struct
import unittest

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_EIP


@unittest.skipUnless(os.environ.get('AC2_ORIGINAL_EXE'), 'requires owned exact executable')
class NativeFilterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        import hashlib
        from pathlib import Path
        data = Path(os.environ['AC2_ORIGINAL_EXE']).read_bytes()
        assert hashlib.sha256(data).hexdigest() == '23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92'
        cls.pe = pefile.PE(data=data)

    def execute(self, ranges, stale=False, resident=True):
        m = Uc(UC_ARCH_X86, UC_MODE_32)
        # Original filter, descriptor getters, binary search and lookup all run
        # unmodified. Only synthetic objects/descriptors replace live state.
        m.mem_map(0x400000, 0x216e000)
        m.mem_write(0x400000, self.pe.get_memory_mapped_image())
        m.mem_map(0x10000, 0x90000)
        m.mem_map(0x3000000, 0x10000)
        def word(a, v):
            m.mem_write(a, struct.pack('<I', v))
        word(0x10034, 0x20000)
        word(0x20000, 0x30000)
        word(0x20008, 0x80000000 if resident else 0)
        word(0x300e4, 7)
        word(0x10030, 8 if stale else 7)
        word(0x1002c, 0x40000)
        word(0x40008, 0x30000)
        word(0x30074, 0x50000)
        word(0x30078, len(ranges) | ((len(ranges) | 0x8000) << 16))
        word(0x20c9204, 10 | (20 << 16))
        word(0x209bc8c, 30 | (40 << 16))
        for i, (first, last) in enumerate(ranges):
            c, table, getter, descriptor = (0x60000+i*0x100, 0x70000+i*0x100,
                                           0x80000+i*0x100, 0x90000+i*0x100)
            word(0x50000+i*4, c)
            word(c, table)
            word(table+0x10, getter)
            m.mem_write(getter, b'\xb8'+struct.pack('<I', descriptor)+b'\xc3')
            word(descriptor+0x54, first | (last << 16))
        sp, stop = 0x300f000, 0x10080
        word(sp, stop)
        word(sp+4, 0x10000)
        m.reg_write(UC_X86_REG_ESP, sp)
        m.emu_start(0x18fe9b0, stop, count=100000)
        self.assertEqual(m.reg_read(UC_X86_REG_EIP), stop)
        self.assertEqual(m.reg_read(UC_X86_REG_ESP), sp+4)  # cdecl filter
        result = m.reg_read(UC_X86_REG_EAX) & 255
        expected = not stale and resident and any(
            10 <= a <= b <= 20 or 30 <= a <= b <= 40 for a, b in ranges)
        self.assertEqual(result, int(expected))
        cache = struct.unpack('<I', m.mem_read(0x1002c, 4))[0]
        self.assertEqual(cache, 0 if stale or not resident else 0x40000)

    def test_native_subtrees_and_invalidated_contact(self):
        for ranges in ([], [(1, 1), (25, 25), (50, 50)], [(11, 12)],
                       [(31, 32)], [(11, 25)], [(1, 1), (10, 20), (50, 50)]):
            for stale, resident in ((False, True), (True, True), (False, False)):
                with self.subTest(ranges=ranges, stale=stale, resident=resident):
                    self.execute(ranges, stale, resident)

    def test_native_sorted_descriptor_search(self):
        rng = random.Random(20260907)
        for _ in range(40):
            self.execute([(x, x) for x in sorted(rng.sample(range(1, 65), rng.randrange(30)))])


if __name__ == '__main__':
    unittest.main()
