"""Pure synthetic mailbox tests. Never connect to or mutate a running game."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location(
    "inspector", Path(__file__).resolve().parents[1] / "tools/read_live_inspector.py")
inspector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inspector)


class InspectorTests(unittest.TestCase):
    def test_request_wire_and_boundaries(self):
        self.assertEqual(struct.unpack("<5I", inspector.make_request(128, 0xfffff000, 4096)),
                         (0x51523343, 1, 128, 0xfffff000, 4096))
        self.assertEqual(len(inspector.make_request(1, 1, 1)), 20)
        for args in ((0, 1, 1), (129, 1, 1), (True, 1, 1), (1, 0, 1),
                     (1, 0xffffffff, 2), (1, 1, 0), (1, 1, 4097), (1, 1, True)):
            with self.subTest(args=args), self.assertRaises(ValueError):
                inspector.make_request(*args)

    def report(self, **changes):
        result = dict(id=1, address=4096, requestedBytes=80, readError=0, stable=True,
                      sha256="a" * 64, elapsedMs=100, atomicSnapshot=False)
        result.update(changes)
        return json.dumps(result)

    def test_stable_and_failed_reads_are_distinct(self):
        self.assertTrue(inspector.validate_report(self.report(), 1, 4096, 80)["stable"])
        result = inspector.validate_report(self.report(stable=False, sha256="", readError=299), 1, 4096, 80)
        self.assertFalse(result["stable"])

    def test_bad_or_mismatched_response_refused(self):
        for key, value in (("id", 2), ("id", True), ("address", 8192), ("requestedBytes", 79),
                           ("stable", 1), ("atomicSnapshot", True), ("readError", 1),
                           ("elapsedMs", -1), ("elapsedMs", True), ("elapsedMs", 930001),
                           ("sha256", ""), ("sha256", None), ("sha256", "g" * 64)):
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                inspector.validate_report(self.report(**{key: value}), 1, 4096, 80)
        with self.assertRaises(ValueError):
            inspector.validate_report(self.report(stable=False), 1, 4096, 80)
        with self.assertRaises(ValueError):
            inspector.validate_report('{"id":2,' + self.report()[1:], 1, 4096, 80)
        with self.assertRaises(ValueError):
            inspector.validate_report("[]", 1, 4096, 80)


if __name__ == "__main__":
    unittest.main()
