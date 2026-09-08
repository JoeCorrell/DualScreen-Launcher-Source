import hashlib
from pathlib import Path
import re
import struct
import unittest

PROJECT=Path(__file__).resolve().parents[1]
WORKSPACE=PROJECT.parents[2]


class NamedArtTests(unittest.TestCase):
    def test_packaged_named_icons_match_catalog(self):
        entries=re.findall(r'\{"([A-Za-z0-9_/-]+)","([a-f0-9]{64})"\}',(PROJECT/'src/NamedArtCatalog.h').read_text())
        self.assertEqual(len(entries),84)
        self.assertEqual(len({key.lower() for key,_ in entries}),84)
        self.assertEqual(sum(key.startswith('modules/') for key,_ in entries),16)
        total=0
        for key,digest in entries:
            path=WORKSPACE/'launcher/src/main/assets/mods/crysis3/payload/native/assets'/(key+'.rgba')
            self.assertNotIn('..',key)
            data=path.read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(),digest)
            magic,version,width,height=struct.unpack_from('<4sIII',data)
            self.assertEqual((magic,version),(b'C3RA',1))
            self.assertTrue(0<width<=2048 and 0<height<=2048)
            self.assertEqual(len(data),16+width*height*4)
            total+=len(data)-16
        self.assertLess(total,32*1024*1024)

    def test_five_pages_and_no_utility_placeholder(self):
        model=(PROJECT/'src/Model.h').read_text()
        enum=re.search(r'enum class Page[^\{]+\{([^}]+)\}',model).group(1)
        self.assertEqual([x.strip() for x in enum.split(',')],['Map','Weapons','Suit','Objectives','Status','Count'])
        catalog=(PROJECT/'src/NativeWeaponCatalog.h').read_text()
        self.assertIn('{"NoWeapon","utility",1,',catalog)
        self.assertIn('{"PickAndThrowWeapon","medium",0,',catalog)
        self.assertIn('{"Hammer","secondary",1,',catalog)
        self.assertIn('{"SCAR","primary",1,',catalog)


if __name__=='__main__': unittest.main()
