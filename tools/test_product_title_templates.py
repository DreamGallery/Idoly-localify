import unittest
import json
from pathlib import Path
import tempfile

from numeric_text_templates import matches_any
from product_title_templates import derive_templates, main


class ProductTitleTemplatesTests(unittest.TestCase):
    def test_consistent_reviewed_serials_generate_exact_stem_only(self):
        entries = [{'source': 'イベント現像フィルムパック1', 'translation': '活动冲洗胶卷礼包1'},
                   {'source': 'イベント現像フィルムパック2', 'translation': '活动冲洗胶卷礼包2'},
                   {'source': '玩家名字1', 'translation': '玩家姓名1'}]
        templates, pending = derive_templates(entries, ['イベント現像フィルムパック'])
        self.assertEqual(pending, [])
        self.assertTrue(matches_any('イベント現像フィルムパック3', templates))
        self.assertTrue(matches_any('活动冲洗胶卷礼包003', templates, rendered=True))
        for text in ['玩家名字3', 'イベント現像フィルムパック用户',
                     'イベント現像フィルムパック３', 'イベント現像フィルムパック1234567',
                     'イベント現像フィルムパック3追加', '用户イベント現像フィルムパック3',
                     'イベント現像フィルムパック3\n']:
            self.assertFalse(matches_any(text, templates), text)

    def test_single_serial_requires_explicit_confirmation_not_duplicate_votes(self):
        entries = [{'source': '特別パック1', 'translation': '特别礼包1'}] * 2
        self.assertEqual(derive_templates(entries, ['特別パック']), ({}, ['特別パック']))
        self.assertEqual(derive_templates(entries, ['特別パック'],
                         confirmed_single_stems=['特別パック']),
                         ({'特別パック{number}': '特别礼包{number}'}, []))

    def test_conflict_and_changed_number_are_rejected(self):
        entries = [{'source': 'お得パック1', 'translation': '超值礼包1'},
                   {'source': 'お得パック2', 'translation': '优惠礼包2'}]
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            derive_templates(entries, ['お得パック'])
        with self.assertRaisesRegex(ValueError, 'preserve'):
            derive_templates([{'source': 'お得パック01', 'translation': '超值礼包1'}],
                             ['お得パック'])

    def test_cli_exports_candidates_and_pending_without_changing_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            titles, config, output = [root / name for name in ('titles.json', 'config.json', 'report.json')]
            titles.write_text(json.dumps({'kind': 'reviewed_product_titles', 'entries': [
                {'source': '特別パック1', 'translation': '特别礼包1'}]}))
            config.write_text(json.dumps({'approved_stems': ['特別パック']}))
            before = titles.read_bytes()
            args = ['--titles', str(titles), '--config', str(config), '--output', str(output)]
            self.assertEqual(main(args), 0)
            self.assertEqual(json.loads(output.read_text())['pending_stems'], ['特別パック'])
            config.write_text(json.dumps({'approved_stems': ['特別パック'],
                                          'confirmed_single_stems': ['特別パック']}))
            main(args)
            self.assertEqual(json.loads(output.read_text())['templates'],
                             {'特別パック{number}': '特别礼包{number}'})
            self.assertEqual(titles.read_bytes(), before)

    def test_cli_does_not_treat_unselected_model_suggestion_as_reviewed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            titles, config = root / 'titles.json', root / 'config.json'
            titles.write_text(json.dumps({'kind': 'shop_detail_title_model_review', 'entries': [
                {'source': '特別パック1', 'model_suggestion': '特别礼包1'}]}))
            config.write_text(json.dumps({'approved_stems': ['特別パック']}))
            with self.assertRaisesRegex(ValueError, 'explicit reviewed'):
                main(['--titles', str(titles), '--config', str(config), '--output', str(root/'out.json')])
