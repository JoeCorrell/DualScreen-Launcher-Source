import importlib.util
from pathlib import Path
import unittest

PROJECT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('suit_actions', PROJECT/'tools/audit_suit_actions.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class SuitActionEvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.capture = PROJECT.parents[2]/'.work/crysis3/live-190400/capture'
        if not cls.capture.is_dir():
            raise unittest.SkipTest('Private exact-image capture required')
        cls.text = (cls.capture/'loaded-text.bin').read_bytes()
        cls.rdata = (cls.capture/'loaded-rdata.bin').read_bytes()

    def test_exact_action_chain(self):
        result = audit.audit(self.capture)
        self.assertEqual([r['callback'] for r in result['actions']], ['0x380f2b30', '0x380f2be0'])
        self.assertEqual(result['callbackStackBytes'], 16)
        self.assertEqual(result['actorInputField'], '0x1108')
        self.assertFalse(result['runtimeActionVerified'])

    def test_mutated_code_rejected(self):
        for address in (0x38193D08, 0x380F6EA4, 0x380F2BD8, 0x380F2BE4,
                        0x380F2C13, 0x380F0E58, 0x380F6886, 0x380D952B):
            with self.subTest(address=hex(address)):
                data = bytearray(self.text)
                data[address-audit.TEXT_BASE] ^= 1
                with self.assertRaises(ValueError):
                    audit.inspect(data, self.rdata)

    def test_mutated_name_and_actor_methods_rejected(self):
        for address in (0x399C02A0, 0x399C02B4, 0x399B5940+0x1C4):
            with self.subTest(address=hex(address)):
                data = bytearray(self.rdata)
                data[address-audit.RDATA_BASE] ^= 1
                with self.assertRaises(ValueError):
                    audit.inspect(self.text, data)

    def test_truncated_evidence_rejected(self):
        with self.assertRaises(ValueError):
            audit.inspect(b'', self.rdata)
        with self.assertRaises(ValueError):
            audit.inspect(self.text, b'')


if __name__ == '__main__':
    unittest.main()
