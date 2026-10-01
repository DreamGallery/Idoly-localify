import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

try:
    from .audit_shop_catalog import audit_catalog, main
except ImportError:
    from audit_shop_catalog import audit_catalog, main


class ShopCatalogAuditTests(unittest.TestCase):
    def setUp(self):
        self.catalog = {'schema_version': 1, 'complete': True, 'shops': [
            {'id': 'shop/a', 'name': 'Shop', 'status': 'ok', 'requests': {'account': 'PRIVATE'},
             'items': [{'id': 'item/b', 'name': '衣装パック', 'description': 'チケット×12',
                        'account': 'PRIVATE', 'rewardId': 4, 'source_method': 'specialPackage'}]}],
             'dokanInfos': [{'id': 9, 'name': '告知', 'description': '', 'linkTitle': '詳細',
                            'unknown': 'PRIVATE'}]}
        self.dictionary = {'text': {'衣装パック': '服装礼包'},
                           'numeric_text_templates': {'チケット×{number}': '券×{number}'}}

    def test_exact_numeric_pending_non_cjk_empty_and_whitelist(self):
        report, pending = audit_catalog(self.catalog, self.dictionary)
        self.assertTrue(report['complete'])
        self.assertFalse(report['coverage_complete'])
        self.assertEqual(report['counts'], {'empty': 1, 'exact_text': 1, 'non_cjk': 1,
                                           'numeric_template': 1, 'pending': 2})
        self.assertEqual(pending, {'text': {'告知': '告知', '詳細': '詳細'}})
        self.assertNotIn('PRIVATE', json.dumps([report, pending]))
        record = next(x for x in report['records'] if x['coverage'] == 'exact_text')
        self.assertEqual(record['identity'], ['shop', 'shop/a', 'item', 'item/b', 'name'])
        self.assertEqual(record['source_sha'], hashlib.sha256('衣装パック'.encode()).hexdigest())

    def test_same_special_package_duplicate_is_not_counted_twice(self):
        report, _ = audit_catalog(self.catalog, self.dictionary)
        self.catalog['shops'][0]['items'] *= 2
        self.assertEqual(audit_catalog(self.catalog, self.dictionary)[0], report)
        self.catalog['dokanInfos'].append(copy.deepcopy(self.catalog['dokanInfos'][0]))
        self.assertEqual(audit_catalog(self.catalog, self.dictionary)[0], report)

    def test_conflict_and_delimiter_ids_are_unambiguous(self):
        conflict = copy.deepcopy(self.catalog['shops'][0]['items'][0])
        conflict['name'] = '別衣装'
        self.catalog['shops'][0]['items'].append(conflict)
        self.catalog['shops'].append({'id': 'shop', 'name': 'Shop', 'status': 'ok',
                                    'items': [{'id': 'a/item/b', 'name': '衣装パック'}]})
        report, pending = audit_catalog(self.catalog, self.dictionary)
        self.assertFalse(report['complete'])
        self.assertEqual(sum(x['identity_conflict'] for x in report['records']), 2)
        self.assertIn('別衣装', pending['text'])
        self.assertTrue(any(x['identity'] == ['shop','shop','item','a/item/b','name'] for x in report['records']))

    def test_collector_failure_cannot_become_successful_empty_shop(self):
        self.catalog['complete'] = False
        self.catalog['shops'][0].update(status='error', items=[])
        report, _ = audit_catalog(self.catalog, self.dictionary)
        self.assertFalse(report['complete'])
        self.assertFalse(report['collector_complete'])
        self.assertIn('shop_not_complete', {x['kind'] for x in report['issues']})
        self.catalog['complete'] = True
        self.assertFalse(audit_catalog(self.catalog, self.dictionary)[0]['complete'])

    def test_no_master_fuzzy_match_and_new_dictionary_reaudits(self):
        item = self.catalog['shops'][0]['items'][0]
        item['description'] = 'チケット×１２'
        report, pending = audit_catalog(self.catalog, self.dictionary)
        self.assertIn(item['description'], pending['text'])
        self.dictionary['text']['衣装'] = '服装'
        item['name'] = '衣装パック別名'
        self.assertIn(item['name'], audit_catalog(self.catalog, self.dictionary)[1]['text'])
        self.dictionary['text'][item['name']] = '新译名'
        self.assertNotIn(item['name'], audit_catalog(self.catalog, self.dictionary)[1]['text'])

    def test_pending_sources_deduplicate_and_order_is_stable(self):
        self.catalog['shops'][0]['items'].append({'id': 2, 'name': '告知', 'description': '詳細'})
        report, pending = audit_catalog(self.catalog, self.dictionary)
        self.assertEqual(len(pending['text']), 2)
        self.catalog['shops'].reverse()
        self.catalog['shops'][0]['items'].reverse()
        self.assertEqual(audit_catalog(self.catalog, self.dictionary), (report, pending))

    def test_cli_writes_pending_even_when_snapshot_is_incomplete(self):
        self.catalog['complete'] = False
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, value in [('catalog',self.catalog),('dict',self.dictionary)]:
                (root/name).write_text(json.dumps(value))
            result = main(['--catalog',str(root/'catalog'),'--dictionary',str(root/'dict'),
                           '--output',str(root/'report'),'--pending',str(root/'pending')])
            self.assertEqual(result, 2)
            self.assertFalse(json.loads((root/'report').read_text())['complete'])
            self.assertIn('告知', json.loads((root/'pending').read_text())['text'])

    def test_cli_pending_is_not_success_and_inputs_are_fingerprinted(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, value in [('catalog', self.catalog), ('dict', self.dictionary)]:
                (root/name).write_text(json.dumps(value))
            result = main(['--catalog', str(root/'catalog'), '--dictionary', str(root/'dict'),
                           '--output', str(root/'report'), '--pending', str(root/'pending')])
            self.assertEqual(result, 1)
            report = json.loads((root/'report').read_text())
            self.assertTrue(report['complete'])
            self.assertFalse(report['coverage_complete'])
            self.assertEqual(report['catalog_sha256'], hashlib.sha256((root/'catalog').read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
