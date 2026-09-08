import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_weapon_models import collect


class WeaponModelAuditTests(unittest.TestCase):
    def test_patch_precedence_and_material_variants(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory) / 'base'
            patch = Path(directory) / 'patch'
            base.mkdir()
            patch.mkdir()
            (base / 'gun.xml').write_text('<item name="Gun"><geometry><firstperson name="a.cdf"/></geometry></item>')
            (patch / 'gun.xml').write_text('<item name="Gun"><geometry><firstperson name="b.cdf"/></geometry></item>')
            (base / 'variant.xml').write_text('<item name="Variant" inherit="Gun"><geometry><firstperson name="b.cdf" material="cell"/></geometry></item>')
            rows = collect([base, patch])
            self.assertEqual(len(rows), 2)
            self.assertEqual(rows[0]['declaredGeometry'][0]['name'], 'b.cdf')
            self.assertEqual(rows[1]['declaredGeometry'][0]['material'], 'cell')
            self.assertEqual(rows[1]['inherits'], 'Gun')

    def test_missing_root_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                collect([Path(directory) / 'missing'])
