"""Check literal addressing in the actual compiled ARM64 wardrobe blob.

Host C++ mocks cannot catch ADRP placement bugs. Decode the emitted instructions
and resolve each page-relative float load at its real installation address.
"""
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def payload():
    header = (ROOT / 'src/core/loader/smo_outfit_code.h').read_text()
    code = header.split('Code{{', 1)[1].split('}};', 1)[0]
    words = [int(x, 16) for x in re.findall(r'0x([0-9A-F]{8})U', code)]
    return header, words, b''.join(struct.pack('<I', w) for w in words)


def float_loads(words):
    for i, word in enumerate(words):
        if word & 0x9F000000 != 0x90000000:  # ADRP
            continue
        register = word & 31
        immediate = ((word >> 29) & 3) | (((word >> 5) & 0x7FFFF) << 2)
        if immediate & (1 << 20):
            immediate -= 1 << 21
        for load in words[i + 1:i + 4]:
            if load & 0xFFC00000 == 0xBD400000 and (load >> 5) & 31 == register:
                yield i * 4, immediate * 4096, ((load >> 10) & 4095) * 4
                break


def resolved_literal(blob, placement, pc, page_delta, offset):
    target = ((placement + pc) & ~4095) + page_delta + offset
    relative = target - placement
    if relative < 0 or relative + 4 > len(blob):
        raise ValueError('Literal outside installed helper')
    return blob[relative:relative + 4]


class WardrobeRelocationTest(unittest.TestCase):
    def test_compiled_pose_constants_survive_page_aligned_installation(self):
        header, words, blob = payload()
        self.assertIn('CodeAlignment = 0x1000U', header)
        self.assertIn('CodeOffset = CodeAlignment', header)
        loads = list(float_loads(words))
        self.assertGreaterEqual(len(loads), 4)
        constants = set()
        for load in loads:
            intended = resolved_literal(blob, 0, *load)
            constants.add(round(struct.unpack('<f', intended)[0], 2))
            # Exercise nonzero module bases as well as the actual reservation.
            for placement in (0x25AA000, 0x1A2A053000, 0x700000000):
                self.assertEqual(intended, resolved_literal(blob, placement, *load))
        self.assertTrue({10000000.0, 1.01, 0.99}.issubset(constants))

    def test_previous_128_byte_placement_corrupts_the_norm_limit(self):
        _, words, blob = payload()
        upper_limits = [load for load in float_loads(words)
                        if abs(struct.unpack('<f', resolved_literal(blob, 0, *load))[0] - 1.01) < 1e-6]
        self.assertTrue(upper_limits)
        for load in upper_limits:
            wrong = struct.unpack('<f', resolved_literal(blob, 0x25A9080, *load))[0]
            self.assertLess(wrong, 0.0)  # Even a correct unit quaternion was rejected.

    def test_both_trampolines_and_code_copy_use_the_aligned_offset(self):
        source = (ROOT / 'src/core/loader/nso.cpp').read_text()
        source = source.split('bool InstallSmoOutfitPayload', 1)[1].split('bool InstallSshd', 1)[0]
        self.assertIn('bridge_offset % SmoOutfitPayload::CodeAlignment', source)
        self.assertIn('EncodeRelativeCall(bridge_offset + 32, bridge_offset + CodeOffset)', source)
        self.assertIn('EncodeRelativeCall(bridge_offset + 96, bridge_offset + CodeOffset)', source)
        self.assertIn('image.data() + module_start + bridge_offset + CodeOffset', source)


if __name__ == '__main__':
    unittest.main()
