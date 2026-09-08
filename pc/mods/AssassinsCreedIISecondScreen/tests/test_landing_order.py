"""Offline instruction/ABI checks. Never launches or controls the game."""
import os
from pathlib import Path
import struct
import sys
import unittest

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import patch_landing_order as patch


class LandingOrderTests(unittest.TestCase):
    def execute(self, edits=(), expected_calls=('landing',), shift=0, replaced_context=False, results=(True,), contact_after_update=False, resolved=0x32000):
        m = Uc(UC_ARCH_X86, UC_MODE_32)
        m.mem_map(0x400000+shift, 0x216f000)
        m.mem_map(0x10000, 0x200000)
        m.mem_map(0x30000000, 0x10000)
        entry, update, land = patch.RETRY_VA+shift, 0x1083410+shift, 0x107a250+shift
        m.mem_write(entry, patch.RETRY_CODE)
        def word(a, value):
            m.mem_write(a, struct.pack('<I', value))
        # Session -> selected world -> current player. Two typed components.
        for a, v in {
            0x2212490:0x20000, 0x2212570:0x21000, 0x210d0:0xffffffff,
            0x2004c:0x22000, 0x2253c:0x23000, 0x23004:0x24000,
            0x2214d00:0x25000, 0x25024:0, 0x24000:0x30000,
            0x30074:0x31000, 0x30078:0x80020002, 0x31000:0x32000, 0x31004:0x33000,
            0x32000:0x1cde3c8, 0x32008:0x30000, 0x32014:0x34000,
            0x32018:0x80010001, 0x33000:0x1e9cd48, 0x33008:0x30000,
            0x33018:0x40000, 0x40000:0x1e7c080, 0x40048:0x50000,
            0x40114:0x30000, 0x400fc:0x40110, 0x40140:0x60000,
            0x50000:0x1e55bb8, 0x50008:0x40000, 0x50362:1,
            0x50180:0x00900000, 0x50010:0x603e0, 0x60000:0x1e7b884,
            0x603e0:0x1e7b34c, 0x605d8:1, 0x60790:0,
            0x60600:1, 0x60604:0x70000, 0x605f0:100, 0x70000:101,
        }.items():
            word(a+shift if a>=0x400000 else a, v)
        for a, value in edits:
            word(a, value)
        registers = {UC_X86_REG_EAX:0x11111111, UC_X86_REG_EBX:0x22222222,
                     UC_X86_REG_ECX:0x50000, UC_X86_REG_EDX:0x44444444,
                     UC_X86_REG_ESI:0x55555555, UC_X86_REG_EDI:0x66666666,
                     UC_X86_REG_EBP:0x77777777}
        for r, v in registers.items():
            m.reg_write(r, v)
        sp, stop = 0x3000f000, 0x10000
        word(sp, stop)
        m.reg_write(UC_X86_REG_ESP, sp)
        fp = {UC_X86_REG_FPCW:0x37f, UC_X86_REG_MXCSR:0x1f80}
        fp.update({UC_X86_REG_XMM0+i:(i+1)*0x112233445566778899 for i in range(8)})
        for r, v in fp.items():
            m.reg_write(r, v)
        calls = []
        resolves = []
        result_index = 0
        expected_flags = 0x202
        m.reg_write(UC_X86_REG_EFLAGS, expected_flags)
        def returned(consumed):
            stack = m.reg_read(UC_X86_REG_ESP)
            dest = struct.unpack('<I', m.mem_read(stack, 4))[0]
            m.reg_write(UC_X86_REG_ESP, stack+4+consumed)
            m.reg_write(UC_X86_REG_EIP, dest)
        def hook(_m, address, _size, _data):
            nonlocal result_index, expected_flags
            if address == 0x5eb010+shift:
                resolves.append(address)
                self.assertEqual(m.reg_read(UC_X86_REG_ECX), 0x40110)
                # The native resolver may refresh index/generation and clobber
                # volatile registers and FP state. Validate preservation too.
                word(0x4011f, 0)
                word(0x300f4, 0)
                m.reg_write(UC_X86_REG_EAX, resolved)
                m.reg_write(UC_X86_REG_ECX, 0xdddddddd)
                m.reg_write(UC_X86_REG_EDX, 0xeeeeeeee)
                m.reg_write(UC_X86_REG_XMM2, 0xffff)
                m.reg_write(UC_X86_REG_FPCW, 0x27f)
                returned(0)
            elif address == update:
                calls.append('update')
                for r, v in fp.items():
                    self.assertEqual(m.reg_read(r), v, 'precheck must preserve original FP inputs')
                if contact_after_update:
                    word(0x32018, 0x80010001)
                expected_flags = 0x247
                self.assertEqual(m.reg_read(UC_X86_REG_ECX), 0x50000)
                for r, v in ((UC_X86_REG_EAX, 0xaabbccdd), (UC_X86_REG_ECX, 0xabcdef01),
                             (UC_X86_REG_EDX, 0x12345678)):
                    m.reg_write(r, v)
                    registers[r] = v
                m.reg_write(UC_X86_REG_EFLAGS, 0x247)
                if replaced_context:
                    word(0x40048, 0x51000)
                    m.mem_unmap(0x50000, 4096)  # original context destroyed by update
                returned(0)
            elif address == land:
                calls.append('landing')
                self.assertEqual(m.reg_read(UC_X86_REG_ECX), 0x50000)
                stack = m.reg_read(UC_X86_REG_ESP)
                self.assertEqual(struct.unpack('<III', m.mem_read(stack+4, 12)), (1, 1, 0))
                accepted = results[min(result_index, len(results)-1)]
                result_index += 1
                if accepted:
                    word(0x50180, 0x00900040)  # only the native routine writes this
                for r in (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX):
                    m.reg_write(r, 0xdeadbeef)
                for r in range(UC_X86_REG_XMM0, UC_X86_REG_XMM0+8):
                    m.reg_write(r, 0xdead)
                m.reg_write(UC_X86_REG_FPCW, 0x27f)
                m.reg_write(UC_X86_REG_MXCSR, 0x3f80)
                m.reg_write(UC_X86_REG_EFLAGS, 0x202)
                returned(12)
        m.hook_add(UC_HOOK_CODE, hook)
        m.emu_start(entry, stop, count=10000)
        self.assertEqual(m.reg_read(UC_X86_REG_EIP), stop)
        self.assertEqual(calls, list(expected_calls))
        self.assertEqual(m.reg_read(UC_X86_REG_ESP), sp+4)
        for r, v in registers.items():
            self.assertEqual(m.reg_read(r), v, 'original update register result preserved')
        for r, v in fp.items():
            self.assertEqual(m.reg_read(r), v, 'floating-point state preserved')
        self.assertEqual(m.reg_read(UC_X86_REG_EFLAGS), expected_flags)


    def test_existing_contact_is_consumed_before_airborne_update(self):
        self.execute()

    def test_recorded_latch_flags_allow_native_landing(self):
        for flags in (0x0c900000, 0x0c940000):
            self.execute([(0x50180, flags)])

    def test_new_contacts_after_update_still_land(self):
        self.execute([(0x32018, 1)], expected_calls=('update','landing'), contact_after_update=True)

    def test_native_rejection_preserves_full_update_and_late_retry(self):
        self.execute(results=(False,True), expected_calls=('landing','update','landing'))
        self.execute(results=(False,), expected_calls=('landing','update','landing'))

    def test_native_cache_resolution_handles_stale_generation_and_index(self):
        self.execute([(0x300f4, 1), (0x40120, 0xfe)])
        self.execute([(0x300f4, 2), (0x40120, 0xff)])

    def test_invalid_resolved_component_is_not_used(self):
        self.execute(resolved=0, expected_calls=('update',))
        self.execute([(0x32000,0)], expected_calls=('update',))
        self.execute([(0x32008,0)], expected_calls=('update',))
        self.execute([(0x32018,1)], expected_calls=('update',))
        self.execute([(0x32014,0)], expected_calls=('update',))

    def test_context_destroyed_by_original_update_is_not_dereferenced(self):
        self.execute(results=(False,), replaced_context=True, expected_calls=('landing','update'))

    def test_modes_pending_transitions_timers_and_invalid_owners(self):
        for a,v in [(0x2212490,0),(0x2212570,0),(0x2004c,0),(0x25024,4),
                    (0x33008,0),(0x40048,0x51000),(0x40114,0),(0x50362,2),
                    (0x50180,0x00900040),(0x50180,0x00900100),(0x50180,0x00d00000),
                    (0x50010,0),(0x605d8,0),(0x605d8,0x101),(0x605d8,0x10001),
                    (0x60790,1),(0x60600,0),(0x60604,0),(0x70000,99),(0x605f4,1)]:
            with self.subTest(address=hex(a),value=v):
                self.execute([(a,v)], expected_calls=('update',))

    def test_relocated_image_runs_original_update_only(self):
        self.execute(shift=0x1000000,expected_calls=('update',))

    def test_selected_world_and_override_clock(self):
        self.execute([(0x210d0,0),(0x210c4,0x26000),(0x26000,0x22000),(0x26008,0x80000000)])
        self.execute([(0x605f8,101),(0x60604,0)])
        self.execute([(0x605f8,99)],expected_calls=('update',))

    @unittest.skipUnless(os.environ.get('AC2_ORIGINAL_EXE'), 'owned executable required')
    def test_pe_preserves_previous_patch_and_original_overlay(self):
        original = Path(os.environ['AC2_ORIGINAL_EXE']).read_bytes()
        previous = patch.contacts.patch(original)
        result = patch.patch(original)
        old, new = pefile.PE(data=previous), pefile.PE(data=result)
        self.assertEqual(new.generate_checksum(), new.OPTIONAL_HEADER.CheckSum)
        self.assertEqual(new.OPTIONAL_HEADER.SizeOfImage, old.OPTIONAL_HEADER.SizeOfImage)
        site = old.get_offset_from_rva(patch.UPDATE_CALL-0x400000)
        for section in old.sections[:-1]:
            a, n = section.PointerToRawData, section.SizeOfRawData
            expected = bytearray(previous[a:a+n])
            if a <= site < a+n:
                expected[site-a:site-a+5] = patch.contacts.rel_call(patch.UPDATE_CALL, patch.RETRY_VA)
            self.assertEqual(result[a:a+n], expected)
        self.assertEqual(new.sections[-1].get_data()[:len(patch.contacts.SEARCH_CODE)], patch.contacts.SEARCH_CODE)
        overlay = pefile.PE(data=original).get_overlay_data_start_offset()
        self.assertEqual(result[overlay:len(original)], original[overlay:])
        with self.assertRaises(ValueError):
            patch.patch(previous)
        with self.assertRaises(ValueError):
            patch.patch(result)


if __name__ == '__main__':
    unittest.main()
