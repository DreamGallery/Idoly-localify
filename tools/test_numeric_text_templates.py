import json
from pathlib import Path
import tempfile
import unittest

from localize import build
from numeric_text_templates import compile_templates, matches_any


SOURCE = '{number}日目のログインボーナスです'
TARGET = '这是第{number}天的登录奖励'


class NumericTextTemplatesTests(unittest.TestCase):
    def test_compiler_requires_reviewed_exact_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.json'
            translation = root / 'zh-Hans.json'
            compiled = root / 'dict.json'
            source.write_text(json.dumps({'text': {}, 'i18n': {},
                'numeric_text_templates': {SOURCE: SOURCE}}, ensure_ascii=False))
            translation.write_text(json.dumps({'text': {}, 'i18n': {},
                'numeric_text_templates': {SOURCE: TARGET}}, ensure_ascii=False))
            result = build(source, translation, compiled, False, story_dir=None,
                           name_glossary=None, term_glossary=None, master_text=None)
            self.assertEqual(result['numeric_text_templates'], {SOURCE: TARGET})
            self.assertEqual(json.loads(compiled.read_text())['numeric_text_templates'],
                             {SOURCE: TARGET})
            translation.write_text(json.dumps({'text': {}, 'i18n': {},
                'numeric_text_templates': {SOURCE: '这是第{user}天的登录奖励'}},
                ensure_ascii=False))
            with self.assertRaises(ValueError):
                build(source, translation, compiled, False, story_dir=None,
                      name_glossary=None, term_glossary=None, master_text=None)

    def test_full_match_accepts_only_ascii_digits(self):
        templates = compile_templates({SOURCE: SOURCE}, {SOURCE: TARGET})
        self.assertTrue(matches_any('2日目のログインボーナスです', templates))
        self.assertTrue(matches_any('这是第8天的登录奖励', templates, rendered=True))
        for value in ('２日目のログインボーナスです',
                      '经理日目のログインボーナスです',
                      '2日目のログインボーナスです。'):
            self.assertFalse(matches_any(value, templates))
        self.assertFalse(matches_any('这是第用户天的登录奖励', templates, rendered=True))

    def test_compiler_caps_template_count_at_native_limit(self):
        sources = {f'条目{i}' + SOURCE: f'条目{i}' + SOURCE for i in range(129)}
        targets = {source: f'对应{i}' + TARGET for i, source in enumerate(sources)}
        with self.assertRaisesRegex(ValueError, 'at most 128'):
            compile_templates(sources, targets)


if __name__ == '__main__':
    unittest.main()
