import math
import unittest

import oead

from export_smo_metadata import plain


class SmoMetadataTests(unittest.TestCase):
    def test_native_conditions_and_order_are_preserved(self):
        data = oead.byml.from_text('''
- WorldName: City
  Scenario: 6
  IsSecret: true
  Conditions: [UnknownFlag, MoonRock]
- WorldName: Snow
  Scenario: -1
''')
        result = plain(data)
        self.assertEqual([row['WorldName'] for row in result], ['City', 'Snow'])
        self.assertEqual(result[0]['Conditions'], ['UnknownFlag', 'MoonRock'])
        self.assertIs(result[0]['IsSecret'], True)
        self.assertEqual(result[1]['Scenario'], -1)

    def test_numeric_values_are_lossless(self):
        self.assertEqual(plain(oead.U64(2**63 + 7)), 2**63 + 7)
        self.assertEqual(plain(oead.S32(-7)), -7)
        self.assertEqual(plain(oead.F32(1.25)), 1.25)

    def test_unknown_and_nonfinite_are_rejected(self):
        for value in (object(), float('nan'), float('inf')):
            with self.assertRaises(ValueError):
                plain(value)


if __name__ == '__main__':
    unittest.main()
