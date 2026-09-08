"""Exact private capture closure for the new read-only module/objective paths."""
import hashlib
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

PROJECT = Path(__file__).resolve().parents[1]
WORKSPACE = PROJECT.parents[2]


class OptionalProfileTests(unittest.TestCase):
    def test_all_optional_fingerprints(self):
        capture = WORKSPACE / '.work/crysis3/live-190400/capture'
        if not capture.is_dir():
            self.skipTest('Private runtime captures are not distributed')
        sections = [(0x37001000, 'loaded-text.bin', 'e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f'),
                    (0x3987f000, 'loaded-rdata.bin', '376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe')]
        buffers = []
        for base, name, digest in sections:
            data = (capture / name).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), digest)
            buffers.append((base, data))
        for profile, expected_count in [('NativeSuitProfile', 16), ('NativeObjectiveProfile', 15)]:
            source = (PROJECT / f'src/{profile}.h').read_text()
            entries = re.findall(r'\{(0x[0-9a-f]+),([0-9]+),"([0-9a-f]{64})"\}', source)
            self.assertEqual(len(entries), expected_count)
            for address, count, digest in entries:
                address, count = int(address, 16), int(count)
                self.assertTrue(0 < count <= 4096)
                matches = [data[address-base:address-base+count] for base, data in buffers
                           if base <= address and address+count <= base+len(data)]
                self.assertEqual(len(matches), 1)
                self.assertEqual(hashlib.sha256(matches[0]).hexdigest(), digest)

    def test_module_display_names_are_exact_game_text(self):
        path = WORKSPACE / '.research/crysis3/extracted/English/text_ui_messages.xml'
        if not path.is_file():
            self.skipTest('Private game localization is not distributed')
        data = path.read_bytes()
        self.assertEqual(hashlib.sha256(data).hexdigest(), '608a7037a7f6e11feb76b29970365b2e425bdd93bbed0aeee9678063a651d96e')
        ns = '{urn:schemas-microsoft-com:office:spreadsheet}'
        entries = {}
        for row in ET.fromstring(data).iter(ns+'Row'):
            values = [cell.text or '' for cell in row.findall(ns+'Cell/'+ns+'Data')]
            if len(values) >= 3:
                entries[values[0]] = values[2]
        source = (PROJECT / 'src/ReadOnlyTrial.h').read_text()
        names = re.findall(r'\{"([A-Za-z]+)","([A-Za-z ]+)"\}', source)
        self.assertEqual(len(names), 16)
        for module, label in names:
            self.assertEqual(entries['ui_module_'+module], label)


if __name__ == '__main__':
    unittest.main()
