import importlib.util
import hashlib
from pathlib import Path
import re
import struct
import unittest

PROJECT=Path(__file__).resolve().parents[1]
WORKSPACE=PROJECT.parents[2]
spec=importlib.util.spec_from_file_location('stat_audit',PROJECT/'tools/audit_stat_tables.py')
audit=importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class StatisticsEvidenceTests(unittest.TestCase):
    def test_exact_names_getters_and_live_table(self):
        capture=WORKSPACE/'.work/crysis3/live-190400/capture'
        exe=WORKSPACE/'.research/crysis3/original/Bin32/Crysis3.exe'
        if not capture.is_dir() or not exe.is_file():
            self.skipTest('Private capture required')
        result=audit.audit(exe,capture)
        integer,floating=result['tables']
        self.assertEqual(integer['entries'][62]['name'],'EIPS_HeadShotKills')
        self.assertEqual(integer['entries'][114]['name'],'EIPS_HackingChallengesCompleted')
        self.assertEqual(integer['entries'][157]['counterOffset'],'0x4a0')
        self.assertEqual(floating['entries'][1]['name'],'EFPS_DistanceRan')
        self.assertFalse(result['runtimePublicationVerified'])
        live=WORKSPACE/'.work/crysis3/live-234100/read6/read-6.bin'
        if live.is_file():
            expected=b''.join(struct.pack('<I',int(e['nameAddress'],16)) for t in result['tables'] for e in t['entries'])
            self.assertEqual(live.read_bytes(),expected)

    def test_native_stats_profile(self):
        capture=WORKSPACE/'.work/crysis3/live-190400/capture'
        live=WORKSPACE/'.work/crysis3/live-234100/read6/read-6.bin'
        if not capture.is_dir() or not live.is_file():
            self.skipTest('Private captures required')
        buffers=[(0x37001000,(capture/'loaded-text.bin').read_bytes()),
                 (0x3987f000,(capture/'loaded-rdata.bin').read_bytes()),(0x3b826930,live.read_bytes())]
        entries=re.findall(r'\{(0x[0-9a-f]+),([0-9]+),"([0-9a-f]{64})"\}',(PROJECT/'src/NativeStatsRead.h').read_text())
        self.assertEqual(len(entries),9)
        for address,length,digest in entries:
            address,length=int(address,16),int(length)
            matches=[data[address-base:address-base+length] for base,data in buffers if base<=address and address+length<=base+len(data)]
            self.assertEqual(len(matches),1)
            self.assertEqual(hashlib.sha256(matches[0]).hexdigest(),digest)

    def test_controlled_live_counter_changes(self):
        root=WORKSPACE/'.work/crysis3/live-234100'
        if not (root/'read10/read-10.bin').is_file():
            self.skipTest('Private controlled test required')
        before=(root/'read8/read-8.bin').read_bytes()
        after=(root/'read10/read-10.bin').read_bytes()
        for index in (117,158,159):
            self.assertGreater(struct.unpack_from('<I',after,index*4)[0],struct.unpack_from('<I',before,index*4)[0])
        for index in (1,2):
            self.assertGreater(struct.unpack_from('<f',after,(258+index)*4)[0],struct.unpack_from('<f',before,(258+index)*4)[0])


if __name__=='__main__':unittest.main()
