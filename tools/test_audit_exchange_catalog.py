import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
try:
    from .audit_exchange_catalog import audit_exchange_catalog, main
except ImportError:
    from audit_exchange_catalog import audit_exchange_catalog, main


class ExchangeAuditTests(unittest.TestCase):
    def setUp(self):
        self.catalog = {'schema': 1, 'complete': True, 'conflicts': [], 'booths': [
            {'id': 'booth/a', 'name': '交換所', 'status': 'success', 'requiredResourceId': 'PRIVATE',
             'exchanges': [{'id': 'item/b', 'name': 'アイテム×5', 'description': '紹介',
                            'rewardId': 'KNOWN_MASTER_REWARD', 'leftCount': 'PRIVATE'}]}]}
        self.dictionary = {'text': {'交換所': '交换所'},
                           'numeric_text_templates': {'アイテム×{number}': '物品×{number}'}}

    def test_exact_numeric_pending_and_stable_identity(self):
        report, pending = audit_exchange_catalog(self.catalog, self.dictionary)
        self.assertTrue(report['complete'])
        self.assertEqual(report['booth_count'], 1)
        self.assertEqual(report['exchange_item_count'], 1)
        self.assertEqual(report['counts'], {'exact_text': 1, 'numeric_template': 1, 'pending': 1})
        self.assertEqual(pending, {'text': {'紹介': '紹介'}})
        self.assertIn(['exchange','booth/a','item','item/b','name'], [r['identity'] for r in report['records']])
        self.assertNotIn('PRIVATE', json.dumps([report,pending]))
        self.assertNotIn('KNOWN_MASTER_REWARD', json.dumps([report,pending]))

    def test_same_item_dedup_and_source_conflict(self):
        report, pending = audit_exchange_catalog(self.catalog,self.dictionary)
        item = self.catalog['booths'][0]['exchanges'][0]
        self.catalog['booths'][0]['exchanges'].append(copy.deepcopy(item))
        self.assertEqual(audit_exchange_catalog(self.catalog,self.dictionary),(report,pending))
        self.catalog['booths'][0]['exchanges'][1]['description']='別紹介'
        self.assertFalse(audit_exchange_catalog(self.catalog,self.dictionary)[0]['complete'])

    def test_failed_booth_or_missing_exchanges_is_not_empty_success(self):
        booth=self.catalog['booths'][0]
        booth.update(status='failed',exchanges=[])
        self.assertFalse(audit_exchange_catalog(self.catalog,self.dictionary)[0]['complete'])
        booth['status']='success'; booth.pop('exchanges')
        self.assertFalse(audit_exchange_catalog(self.catalog,self.dictionary)[0]['complete'])

    def test_collector_incomplete_and_reported_conflict_propagate(self):
        self.catalog['complete']=False
        self.assertFalse(audit_exchange_catalog(self.catalog,self.dictionary)[0]['complete'])
        self.catalog['complete']=True
        self.catalog['conflicts']=[{'booth_id':'booth/a','exchange_id':'item/b','field':'description'}]
        report,_=audit_exchange_catalog(self.catalog,self.dictionary)
        self.assertFalse(report['complete'])
        self.assertIn('collector_identity_conflict', {x['kind'] for x in report['issues']})

    def test_whole_source_only_without_reward_or_master_inference(self):
        item=self.catalog['booths'][0]['exchanges'][0]
        item['name']='アイテム×５'
        self.assertIn(item['name'], audit_exchange_catalog(self.catalog,self.dictionary)[1]['text'])
        self.dictionary['text']['紹介の一部']='部分'
        item['description']='紹介の一部未知'
        self.assertIn(item['description'],audit_exchange_catalog(self.catalog,self.dictionary)[1]['text'])
        self.dictionary['text'][item['description']]='新紹介'
        self.assertNotIn(item['description'],audit_exchange_catalog(self.catalog,self.dictionary)[1]['text'])

    def test_scoped_quantity_only_item_names_and_recomputed_totals(self):
        self.dictionary = {'text': {'交換所': '交换所', 'アイテム': '物品', '紹介': '介绍'}}
        item = self.catalog['booths'][0]['exchanges'][0]
        for quantity in ('1', '999999', '3,000', '10,000,000', '999,999,999'):
            item['name'] = 'アイテム×' + quantity
            report, pending = audit_exchange_catalog(self.catalog, self.dictionary)
            self.assertTrue(report['coverage_complete'])
            self.assertEqual(report['counts'], {'exact_text': 2, 'scoped_quantity': 1})
            self.assertEqual(report['distinct_pending_sources'], 0)
            self.assertEqual(pending, {'text': {}})
        for source in ('アイテム×', 'アイテム×1234567', 'アイテム×５',
                       'アイテム×5 extra', 'アイテム×5×2', 'アイテムx5', '未知×5',
                       'アイテム×3,00', 'アイテム×3,0000', 'アイテム×03,000',
                       'アイテム×0,000', 'アイテム×1000,000', 'アイテム×1,000,000,000',
                       'アイテム×1,,000', 'アイテム×1,000,00', 'アイテム×1,０００',
                       'アイテム×3,000×2', '未知×3,000'):
            item['name'] = source
            self.assertIn(source, audit_exchange_catalog(self.catalog, self.dictionary)[1]['text'])
        item['name'] = 'アイテム×5'
        item['description'] = item['name']
        self.catalog['booths'][0]['name'] = item['name']
        report, pending = audit_exchange_catalog(self.catalog, self.dictionary)
        self.assertEqual(report['counts'], {'pending': 2, 'scoped_quantity': 1})
        self.assertEqual(pending, {'text': {item['name']: item['name']}})
        self.assertFalse(report['coverage_complete'])
        self.dictionary['text']['アイテム'] = ''
        with self.assertRaises(ValueError):
            audit_exchange_catalog(self.catalog, self.dictionary)

    def test_scoped_reset_description_strict_native_grammar(self):
        self.dictionary['text'].update({'紹介': '介绍', '在庫リセット': '库存重置'})
        item = self.catalog['booths'][0]['exchanges'][0]
        for date in ('10/1(木) 00:00', '12/31(月) 23:59', '2/29(日) 01:02'):
            item['description'] = '紹介\n在庫リセット\n' + date
            report, pending = audit_exchange_catalog(self.catalog, self.dictionary)
            self.assertTrue(report['coverage_complete'])
            self.assertEqual(report['counts']['scoped_reset_description'], 1)
            self.assertEqual(pending, {'text': {}})
        for date in ('10/1(木)00:00', '2027/10/1(木) 00:00', '4/31(木) 00:00',
                     '2/30(木) 00:00', '13/1(木) 00:00', '10/0(木) 00:00',
                     '10/1(木) 24:00', '10/1(木) 00:60', '10/1(X) 00:00',
                     '１０/1(木) 00:00', '10/1(木) 00:00 extra', '10/1(木) 00:00\n'):
            item['description'] = '紹介\n在庫リセット\n' + date
            self.assertIn(item['description'], audit_exchange_catalog(self.catalog, self.dictionary)[1]['text'])
        source = '紹介\n在庫リセット\n10/1(木) 00:00'
        item.update(name=source, description='未知' + source)
        self.assertNotIn('scoped_reset_description', audit_exchange_catalog(self.catalog, self.dictionary)[0]['counts'])
        item['description'] = source
        self.dictionary['text'].pop('在庫リセット')
        self.assertIn(source, audit_exchange_catalog(self.catalog, self.dictionary)[1]['text'])

    def test_cli_exit_codes_and_input_fingerprints(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = ['--catalog', str(root/'catalog'), '--dictionary', str(root/'dict'),
                    '--output', str(root/'report'), '--pending', str(root/'pending')]
            (root/'catalog').write_text(json.dumps(self.catalog))
            for expected in (1, 0):
                (root/'dict').write_text(json.dumps(self.dictionary))
                self.assertEqual(main(args), expected)
                report = json.loads((root/'report').read_text())
                for key, name in (('catalog_sha256', 'catalog'), ('dictionary_sha256', 'dict')):
                    self.assertEqual(report[key], hashlib.sha256((root/name).read_bytes()).hexdigest())
                self.dictionary['text']['紹介'] = '介绍'

    def test_cli_outputs_distinct_pending_on_failed_snapshot(self):
        self.catalog['complete']=False
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name,value in [('catalog',self.catalog),('dict',self.dictionary)]:
                (root/name).write_text(json.dumps(value))
            self.assertEqual(main(['--catalog',str(root/'catalog'),'--dictionary',str(root/'dict'),
                                  '--output',str(root/'report'),'--pending',str(root/'pending')]),2)
            self.assertEqual(json.loads((root/'pending').read_text()),{'text':{'紹介':'紹介'}})


if __name__=='__main__':
    unittest.main()
