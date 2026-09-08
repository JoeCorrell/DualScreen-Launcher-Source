import gzip
import hashlib
import importlib.util
from pathlib import Path
import struct
import unittest

TOOL = Path(__file__).resolve().parents[1] / "tools/patch_execute_permission.py"
spec = importlib.util.spec_from_file_location("permission", TOOL)
permission = importlib.util.module_from_spec(spec)
spec.loader.exec_module(permission)
ROOT = TOOL.parents[4]


class ExecutePermissionTests(unittest.TestCase):
    def test_wrong_build_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unsupported executable"):
            permission.patch(b"MZ not the verified game")

    def test_shipped_delta_changes_only_derived_permission_byte(self):
        original = (ROOT / ".research/crysis3/original/Bin32/Crysis3.exe").read_bytes()
        output, offset = permission.patch(original)
        self.assertEqual(offset, 0x25F)
        self.assertEqual(original[:offset], output[:offset])
        self.assertEqual(original[offset + 1:], output[offset + 1:])
        self.assertEqual(output[offset] ^ original[offset], 0x20)
        self.assertEqual(hashlib.sha256(output).hexdigest(),
                         "fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055")
        shipped = ROOT / "launcher/src/main/assets/mods/crysis3/payload/compatibility/data-execute.dsdp"
        blob = gzip.decompress(shipped.read_bytes())
        self.assertEqual(blob[:8], permission.MAGIC)
        self.assertEqual(blob[16:48].hex(), permission.ORIGINAL_SHA)
        self.assertEqual(blob[56:88].hex(), hashlib.sha256(output).hexdigest())
        # Independently interpret the actual shipped copy/literal delta.
        cursor, result = 88, bytearray()
        while blob[cursor] != 255:
            opcode = blob[cursor]
            cursor += 1
            if opcode == 0:
                start, length = struct.unpack_from("<QI", blob, cursor)
                cursor += 12
                result.extend(original[start:start + length])
            elif opcode == 1:
                length = struct.unpack_from("<I", blob, cursor)[0]
                cursor += 4
                result.extend(blob[cursor:cursor + length])
                cursor += length
            else:
                self.fail("Unknown delta opcode")
        self.assertEqual(cursor + 1, len(blob))
        self.assertEqual(bytes(result), output)


if __name__ == "__main__":
    unittest.main()
