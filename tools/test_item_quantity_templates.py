import unittest

from item_quantity_templates import derive_item_templates
from numeric_text_templates import matches_any


class ItemQuantityTemplatesTests(unittest.TestCase):
    def test_only_explicit_items_and_full_ascii_quantity_match(self):
        original = {'piece': {'name': 'ブルームピース'}, 'other': {'name': '他のアイテム'}}
        translated = {'piece': {'name': '开花碎片'}, 'other': {'name': '其他道具'}}
        templates, evidence = derive_item_templates(original, translated, ['piece'])
        self.assertEqual(templates, {'ブルームピース×{number}': '开花碎片×{number}'})
        self.assertEqual(evidence[0]['field'], 'name')
        self.assertTrue(matches_any('ブルームピース×10', templates))
        self.assertTrue(matches_any('开花碎片×0010', templates, rendered=True))
        for text in ('他のアイテム×10', '玩家ブルームピース×10', 'ブルームピースx10',
                     'ブルームピース×１０', 'ブルームピース×1000000', 'ブルームピース×10日'):
            self.assertFalse(matches_any(text, templates), text)

    def test_conflicting_reviewed_name_and_missing_ids_rejected(self):
        orig = {'a': {'name': 'ブルームピース'}, 'b': {'name': 'ブルームピース'}}
        zh = {'a': {'name': '开花碎片'}, 'b': {'name': '绽放碎片'}}
        with self.assertRaisesRegex(ValueError, 'Conflicting'):
            derive_item_templates(orig, zh, ['a', 'b'])
        with self.assertRaisesRegex(ValueError, 'absent'):
            derive_item_templates(orig, zh, ['missing'])
