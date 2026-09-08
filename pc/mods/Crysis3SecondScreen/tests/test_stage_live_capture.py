import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location(
    "stage", Path(__file__).resolve().parents[1] / "tools/stage_live_capture.py")
stage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stage)


class StageCaptureTests(unittest.TestCase):
    def test_packet_matches_launcher_wire_contract(self):
        filename = "wscript.exe"
        parameters = "//B //NoLogo F:\\C3R190100\\capture.vbs"
        data = stage.exec_packet(filename, parameters)
        self.assertEqual(len(data), 64)
        self.assertEqual(struct.unpack_from("<BIII", data),
                         (2, len(filename) + len(parameters) + 8, len(filename), len(parameters)))
        end = 13 + len(filename) + len(parameters)
        self.assertEqual(data[13:end], (filename + parameters).encode("ascii"))
        self.assertEqual(data[end:], bytes(64 - end))

    def test_oversized_empty_and_null_commands_rejected(self):
        for name, args in (("", "x"), ("x" * 52, ""), ("a", "b" * 51), ("a\0", "b"), ("a", "b\0")):
            with self.subTest(name=name, args=args):
                with self.assertRaises(ValueError):
                    stage.exec_packet(name, args)

    def test_probe_inspector_and_performance_commands_fit_without_truncation(self):
        for mode in (" p", " i", " e"):
            command = "//B //NoLogo F:\\C3R191000\\capture.vbs" + mode
            packet = stage.exec_packet("wscript.exe", command)
            self.assertEqual(len(packet), 64)
            self.assertEqual(packet[24:24 + len(command)], command.encode("ascii"))


if __name__ == "__main__":
    unittest.main()
