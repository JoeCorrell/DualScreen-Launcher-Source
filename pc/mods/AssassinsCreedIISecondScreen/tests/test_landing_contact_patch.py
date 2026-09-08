"""Execute the candidate x86 bytes with mocked native query/material callbacks.

These checks validate the patch algorithm and ABI, not the cause of a live lock.
Requires unicorn and pefile. AC2_ORIGINAL_EXE enables exact-file PE checks.
"""
import importlib.util
import math
import os
from pathlib import Path
import random
import struct
import unittest

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
    UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)
import pefile

spec = importlib.util.spec_from_file_location(
    'patch', Path(__file__).parents[1] / 'tools/patch_landing_contacts.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class ContactPatchTests(unittest.TestCase):
    def execute(self, contacts, maximum=45.0, base_shift=0, native_query=False):
        """contacts = (normal Z, excluded-by-native-material-filter) pairs."""
        emu = Uc(UC_ARCH_X86, UC_MODE_32)
        entry, query, reject = (patch.PATCH_VA + base_shift,
                                0x5C5900 + base_shift, 0x18FE9B0 + base_shift)
        for addr in (entry, query & ~0xFFF, reject & ~0xFFF, 0x10000):
            emu.mem_map(addr, 4096)
        emu.mem_map(0x100000, 0x200000)
        emu.mem_write(entry, patch.SEARCH_CODE)
        header, records, sp, stop = 0x100000, 0x101000, 0x2FF000, 0x10000
        source_header = struct.pack('<IHH', records, len(contacts), len(contacts) | 0x8000)
        emu.mem_write(header, source_header)
        contact_bytes = bytearray(max(1, len(contacts)) * 64)
        for i, (z, _) in enumerate(contacts):
            struct.pack_into('<4f', contact_bytes, i * 64 + 16, 0.0, 0.0, z, 0.0)
        emu.mem_write(records, bytes(contact_bytes))
        if native_query:
            self.assertEqual(base_shift, 0)
            executable = pefile.PE(os.environ['AC2_ORIGINAL_EXE'])
            emu.mem_write(query, executable.get_data(query - 0x400000, 0x120))
            for page in (0x1CA5000, 0x1CA9000, 0xC15000):
                emu.mem_map(page, 4096)
            for constant, length in ((0x1CA5AEC, 4), (0x1CA9650, 8)):
                emu.mem_write(constant, executable.get_data(constant - 0x400000, length))
            # The game's CRT cosine takes/returns ST(0). Substitute x87 FCOS
            # only; execute the actual unmodified game's contact-query bytes.
            emu.mem_write(0xC15D00, bytes.fromhex('d9ffc3'))
        emu.mem_write(sp, struct.pack('<Iff', stop, 0.0, maximum))
        emu.reg_write(UC_X86_REG_ESP, sp)
        emu.reg_write(UC_X86_REG_ECX, header)
        saved = {UC_X86_REG_EBX: 0x12345678, UC_X86_REG_ESI: 0x11223344,
                 UC_X86_REG_EDI: 0x55667788, UC_X86_REG_EBP: 0xABCD1234}
        for reg, value in saved.items():
            emu.reg_write(reg, value)
        queries, filters = [], []

        def returned(value, consumed):
            stack = emu.reg_read(UC_X86_REG_ESP)
            dest = struct.unpack('<I', emu.mem_read(stack, 4))[0]
            emu.reg_write(UC_X86_REG_EAX, value)
            emu.reg_write(UC_X86_REG_ECX, 0xDEAD)
            emu.reg_write(UC_X86_REG_EDX, 0xBEEF)
            emu.reg_write(UC_X86_REG_ESP, stack + 4 + consumed)
            emu.reg_write(UC_X86_REG_EIP, dest)

        def callback(_emu, address, _size, _user):
            stack = emu.reg_read(UC_X86_REG_ESP)
            if address == query and not native_query:
                ptr, _, count = struct.unpack('<IHH', emu.mem_read(emu.reg_read(UC_X86_REG_ECX), 8))
                count &= 0x3FFF
                first = (ptr - records) // 64
                low, high = struct.unpack('<ff', emu.mem_read(stack + 4, 8))
                self.assertEqual((low, high), (0.0, maximum))
                self.assertLessEqual(first + count, len(contacts))
                queries.append((first, count))
                found = next((i for i in range(first, first + count)
                              if math.cos(math.radians(high)) <= contacts[i][0] <= 1.0), None)
                returned(0 if found is None else records + found * 64, 8)
            elif address == reject:
                ptr = struct.unpack('<I', emu.mem_read(stack + 4, 4))[0]
                index = (ptr - records) // 64
                filters.append(index)
                # Nonzero upper return bytes: original caller tests AL only.
                returned(0xFACE0000 | int(contacts[index][1]), 0)

        emu.hook_add(UC_HOOK_CODE, callback)
        emu.emu_start(entry, stop, count=2000000)
        self.assertEqual(emu.reg_read(UC_X86_REG_EIP), stop, 'bounded routine must return')
        self.assertEqual(emu.reg_read(UC_X86_REG_ESP), sp + 12)
        for reg, value in saved.items():
            self.assertEqual(emu.reg_read(reg), value)
        self.assertEqual(bytes(emu.mem_read(header, 8)), source_header)
        self.assertEqual(bytes(emu.mem_read(records, max(1, len(contacts)) * 64)), bytes(contact_bytes))
        expected = next((i for i, (z, excluded) in enumerate(contacts)
                         if math.cos(math.radians(maximum)) <= z <= 1.0 and not excluded), None)
        self.assertEqual(emu.reg_read(UC_X86_REG_EAX), 0 if expected is None else records + expected * 64)
        self.assertEqual(len(filters), len(set(filters)), 'never retry a rejected record')
        self.assertLessEqual(len(queries), len(contacts) + 1)

    def test_excluded_first_contact_does_not_mask_floor(self):
        self.execute([(1.0, True), (1.0, False)])

    def test_empty_exhausted_and_slope_boundaries(self):
        for contacts in ([], [(1.0, False)], [(1.0, True)],
                         [(1.0, True)] * 300, [(0.0, False), (0.8, False)],
                         [(0.8, True), (0.2, False)], [(float('nan'), False)],
                         [(0.0, True), (1.0, True), (1.0, False)]):
            with self.subTest(contacts=contacts[:4]):
                self.execute(contacts)

    def test_second_slope_range_and_relocated_image(self):
        self.execute([(1.0, True), (0.4, False)], maximum=70.0)
        self.execute([(1.0, True), (1.0, False)], base_shift=0x1000000)

    def test_random_contact_ordering(self):
        rng = random.Random(20260907)
        for _ in range(100):
            self.execute([(rng.uniform(-1, 1), bool(rng.randrange(2)))
                          for _ in range(rng.randrange(30))])

    def test_unrecognized_executable_rejected(self):
        with self.assertRaises(ValueError):
            patch.patch(b'MZ' + bytes(1024))

    @unittest.skipUnless(os.environ.get('AC2_ORIGINAL_EXE'), 'local owned executable not supplied')
    def test_actual_game_contact_query_instructions(self):
        self.execute([(1.0, True), (1.0, False)], native_query=True)
        self.execute([(1.0, True), (0.0, False), (0.4, False)],
                     maximum=70.0, native_query=True)
        self.execute([(1.0, True)] * 4, native_query=True)

    @unittest.skipUnless(os.environ.get('AC2_ORIGINAL_EXE'), 'local owned executable not supplied')
    def test_exact_file_preserves_original_sections_and_overlay(self):
        original = Path(os.environ['AC2_ORIGINAL_EXE']).read_bytes()
        updated = patch.patch(original)
        before, after = pefile.PE(data=original), pefile.PE(data=updated)
        self.assertEqual(after.FILE_HEADER.NumberOfSections, before.FILE_HEADER.NumberOfSections + 1)
        self.assertEqual(after.OPTIONAL_HEADER.AddressOfEntryPoint, before.OPTIONAL_HEADER.AddressOfEntryPoint)
        self.assertEqual(after.OPTIONAL_HEADER.CheckSum, after.generate_checksum())
        changes = set()
        for site in patch.CALL_SITES:
            offset = before.get_offset_from_rva(site - 0x400000)
            changes.update(range(offset, offset + 5))
            self.assertEqual(updated[offset:offset + 5], patch.rel_call(site, patch.PATCH_VA))
        for a, b in zip(before.sections, after.sections):
            self.assertEqual(a.__pack__(), b.__pack__())
            first, last = a.PointerToRawData, a.PointerToRawData + a.SizeOfRawData
            expected = bytearray(original[first:last])
            for i in changes:
                if first <= i < last:
                    expected[i - first] = updated[i]
            self.assertEqual(bytes(expected), updated[first:last])
        overlay = before.get_overlay_data_start_offset()
        self.assertEqual(original[overlay:], updated[overlay:len(original)])
        self.assertEqual(after.sections[-1].get_data()[:len(patch.SEARCH_CODE)], patch.SEARCH_CODE)
        with self.assertRaises(ValueError):
            patch.patch(updated)  # no double patch or silent stacking


if __name__ == '__main__':
    unittest.main()
