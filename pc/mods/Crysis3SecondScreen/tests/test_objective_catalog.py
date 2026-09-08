import hashlib
import unittest
from unittest.mock import patch
import objective_catalog as cat


def workbook(rows):
    return (f'<Workbook xmlns="{cat.NS[1:-1]}"><Worksheet><Table>' + rows + '</Table></Worksheet></Workbook>').encode()


def row(*values):
    return '<Row>' + ''.join('<Cell><Data>' + value + '</Data></Cell>' for value in values) + '</Row>'


class ObjectiveCatalogTests(unittest.TestCase):
    def test_translated_column_and_exact_key(self):
        source = workbook(row('KEY', 'ORIGINAL TEXT', 'TRANSLATED TEXT') + row('example_name', 'Original', 'Translated'))
        self.assertEqual(cat.parse_rows(source), {'example_name': 'Translated'})

    def test_duplicate_or_declarations_rejected(self):
        source = workbook(row('KEY', 'TRANSLATED TEXT') + row('example', 'One') + row('example', 'Two'))
        with self.assertRaises(ValueError): cat.parse_rows(source)
        with self.assertRaises(ValueError): cat.parse_rows(b'<!DOCTYPE invalid>')

    def test_catalog_digest_and_explicit_language(self):
        entries = {'example_name': 'Translated'}
        catalog = {'schema': 1, 'language': 'English', 'sourceSha256': cat.SOURCE_SHA256,
                   'archiveSha256': cat.ARCHIVE_SHA256, 'entries': entries,
                   'entriesSha256': hashlib.sha256(cat.canonical(entries)).hexdigest()}
        with patch.object(cat, 'ENTRIES_SHA256', catalog['entriesSha256']):
            loaded = cat.load(cat.canonical(catalog))
        self.assertIsNone(cat.resolve(loaded, '@example_name', None))
        self.assertIsNone(cat.resolve(loaded, '@example_name', 'French'))
        self.assertIsNone(cat.resolve(loaded, '@missing', 'English'))
        self.assertEqual(cat.resolve(loaded, '@example_name', 'English'), 'Translated')
        catalog['entries']['example_name'] = 'Altered'
        with self.assertRaises(ValueError): cat.load(cat.canonical(catalog))
        catalog['entriesSha256'] = hashlib.sha256(cat.canonical(catalog['entries'])).hexdigest()
        with self.assertRaises(ValueError): cat.load(cat.canonical(catalog))

    def test_duplicate_json_rejected(self):
        with self.assertRaises(ValueError): cat.load(b'{"schema":1,"schema":1}')

    def test_string_limits_count_utf8_bytes(self):
        header = row('KEY', 'TRANSLATED TEXT')
        source = workbook(header + row('k' * 191, '\u00e9' * 2048))
        self.assertEqual(len(cat.parse_rows(source)['k' * 191].encode('utf-8')), 4096)
        for key, text in [('k' * 192, 'Text'), ('valid', '\u00e9' * 2049), ('bad key', 'Text'), ('valid', '')]:
            with self.assertRaises(ValueError): cat.parse_rows(workbook(header + row(key, text)))

    def test_sparse_columns_and_bad_indices(self):
        header = '<Row><Cell xmlns:ss="' + cat.NS[1:-1] + '" ss:Index="2"><Data>KEY</Data></Cell><Cell><Data>TRANSLATED TEXT</Data></Cell></Row>'
        record = '<Row><Cell/><Cell><Data>example</Data></Cell><Cell><Data>Text</Data></Cell></Row>'
        self.assertEqual(cat.parse_rows(workbook(header + record)), {'example': 'Text'})
        for index in ['0', '17', '-1']:
            invalid = header.replace('ss:Index="2"', 'ss:Index="' + index + '"')
            with self.assertRaises(ValueError): cat.parse_rows(workbook(invalid + record))


if __name__ == '__main__':
    unittest.main()
