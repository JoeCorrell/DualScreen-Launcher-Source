"""Synthetic format tests; no copyrighted resources or real saves required."""
import struct
import unittest

from la_msbt import decode


def fixture(message, encoding=0, endian="<"):
    def section(tag, payload):
        out = tag + struct.pack(endian + "I", len(payload)) + bytes(8) + payload
        return out + bytes(-len(out) % 16)
    label = struct.pack(endian + "III", 1, 1, 12) + b"\x04Name" + struct.pack(endian + "I", 0)
    text = struct.pack(endian + "II", 1, 8) + message
    body = section(b"LBL1", label) + section(b"TXT2", text)
    header = bytearray(32)
    header[:8] = b"MsgStdBn"
    header[8:10] = b"\xff\xfe" if endian == "<" else b"\xfe\xff"
    header[12:14] = bytes([encoding, 3])
    struct.pack_into(endian + "H", header, 14, 2)
    struct.pack_into(endian + "I", header, 18, len(header) + len(body))
    return bytes(header) + body


class MessageTests(unittest.TestCase):
    def test_utf8_preserves_unicode(self):
        self.assertEqual(decode(fixture("Roc’s Feather\0".encode())), {"Name": "Roc’s Feather"})

    def test_utf16_both_endiannesses(self):
        for endian, encoding in (("<", "utf-16le"), (">", "utf-16be")):
            self.assertEqual(decode(fixture("海\0".encode(encoding), 1, endian)), {"Name": "海"})

    def test_utf8_controls_use_one_byte_marker(self):
        raw = b"A\x0e" + struct.pack("<HHH", 1, 2, 2) + b"XY" + b"B\x0f" + struct.pack("<HH", 1, 2) + b"C\0"
        self.assertEqual(decode(fixture(raw)), {"Name": "ABC"})

    def test_utf16_controls_use_two_byte_marker(self):
        raw = "A".encode("utf-16le") + struct.pack("<HHHH", 14, 1, 2, 2) + b"XY" + "B\0".encode("utf-16le")
        self.assertEqual(decode(fixture(raw, 1)), {"Name": "AB"})

    def test_truncation_rejected(self):
        blob = fixture(b"A\0")
        for length in (0, 7, 31, len(blob) - 1):
            with self.assertRaises(ValueError):
                decode(blob[:length])

    def test_unsupported_encoding_rejected(self):
        with self.assertRaises(ValueError):
            decode(fixture(b"A\0", 2))

    def test_unterminated_text_rejected(self):
        with self.assertRaises(ValueError):
            decode(fixture(b"No terminator"))

    def test_oversized_control_rejected(self):
        with self.assertRaises(ValueError):
            decode(fixture(b"\x0e" + struct.pack("<HHH", 1, 2, 65535) + b"\0"))


if __name__ == "__main__":
    unittest.main()
