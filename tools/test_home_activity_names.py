import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from home_activity_names import PATHS, compile_names, may_match, translate
from localize import build

ROOT = Path(__file__).resolve().parents[1]


class HomeActivityNamesTests(unittest.TestCase):
    def test_compiler_pairs_names_without_flattening_or_level_literals(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, translated, output = [root / name for name in ('source.json', 'zh.json', 'dict.json')]
            source.write_text(json.dumps({'text': {}, 'i18n': {}, 'home_activity_names': {'サイン会': 'サイン会'}}))
            translated.write_text(json.dumps({'text': {}, 'i18n': {}, 'home_activity_names': {'サイン会': '签名会'}}))
            result = build(source, translated, output, False, story_dir=None,
                           name_glossary=None, term_glossary=None, master_text=None)
            self.assertEqual(result['home_activity_names'], {'サイン会': '签名会'})
            self.assertNotIn('サイン会', result['text'])
            self.assertNotIn('サイン会 Lv28', result['text'])
            self.assertEqual(result['numeric_text_templates'], {})

    def test_scoped_dynamic_levels_and_rendered_prefilter(self):
        names = compile_names({'サイン会': 'サイン会'}, {'サイン会': '签名会'})
        for path in PATHS:
            self.assertEqual(translate('サイン会 Lv28', path, names), '签名会 Lv28')
            self.assertEqual(translate('サイン会 Lv048', path, names), '签名会 Lv048')
            self.assertIsNone(translate('签名会 Lv28', path, names))
        self.assertTrue(may_match('サイン会 Lv28', names))
        self.assertTrue(may_match('签名会 Lv28', names))
        for text in ('サイン会 Lv２８', 'サイン会 Lv-1', 'サイン会 Lv1.5', 'サイン会 Lv1234567',
                     'サイン会 Lv28\n', 'player Lv28', 'サイン会 Lv{name}'):
            self.assertFalse(may_match(text, names))
        self.assertIsNone(translate('サイン会 Lv28', 'Unknown/NameBase/Name', names))

    def test_validation_rejects_unpaired_names_tokens_limits_and_chains(self):
        self.assertEqual(compile_names({}, {}), {})
        for source, translated in [({'サイン会': 'サイン会'}, {}), ({'サイン会': 'oops'}, {'サイン会': '签名会'}),
                ({'': ''}, {'': '签名会'}), ({'姓名': '姓名'}, {'姓名': '{user}'}),
                ({'x' * 65: 'x' * 65}, {'x' * 65: '中文'}),
                ({'player name': 'player name'}, {'player name': '名字'}),
                ({'A': 'A', 'B': 'B'}, {'A': 'B', 'B': 'C'}),
                ({str(i): str(i) for i in range(65)}, {str(i): '译' for i in range(65)})]:
            with self.subTest(source=source):
                with self.assertRaises(ValueError): compile_names(source, translated)

    @unittest.skipUnless(shutil.which('c++'), 'A C++ compiler is required')
    def test_native_matcher_uses_same_bounded_path_and_name_rules(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'home-activity-test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                str(ROOT / 'tools/test_home_activity_name_matcher.cpp'), '-o', str(binary)],
                check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
