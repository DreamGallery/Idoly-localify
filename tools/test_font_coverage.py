"""The test APK font gate checks all reviewed translation sources."""

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from font_coverage import (audit_codepoints, replaces_all_text,
                           require_coverage, source_fields, translated_fields)


class FontCoverageTests(unittest.TestCase):
    def test_missing_visible_glyph_is_rejected_but_variation_selector_is_ignored(self):
        covered = {ord(character) for character in "中文▶"}
        report = audit_codepoints(covered, [
            ("story", "中文"), ("ui", "▶︎"), ("master", "龙"),
        ])
        self.assertEqual(report["fields"], 3)
        self.assertEqual(report["affected_fields"], 1)
        self.assertEqual(report["missing_characters"], "龙")
        with self.assertRaisesRegex(ValueError, "misses 1 visible"):
            require_coverage(report)

        private_use = audit_codepoints(covered, [("icon", "\ue000")])
        self.assertEqual(private_use["missing_characters"], "\ue000")

    def test_replacement_character_is_rejected_even_when_font_contains_it(self):
        report = audit_codepoints({ord("�")}, [("Story/one", "�")])
        self.assertEqual(report["replacement_character_count"], 1)
        with self.assertRaisesRegex(ValueError, "U\\+FFFD"):
            require_coverage(report)

    def test_reads_story_master_ui_and_glossaries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "localify"
            files = {
                "translations/story/adv_one.json": {"translations": {"1:text:1": "剧情"}},
                "translations/master/zh-Hans/Story.json": {"one": {"name": "标题"}},
                "translations/ui/zh-Hans.json": {"text": {"元": "界面"}},
                "translations/glossary/names.json": {"名": "姓名"},
                "translations/glossary/terms.json": {"团": "组合"},
            }
            for name, data in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
            values = {value for _, value in translated_fields(root)}
            self.assertEqual(values, {"剧情", "标题", "界面", "姓名", "组合"})

    def test_whole_font_mode_includes_original_game_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "localify"
            originals = {
                "../HoshimiToolkit/cache/resource/adventure/adv_one.txt": "原始对白",
                "translations/master/orig/Story.json": '{"1":"原版标题"}',
                "translations/ui/source.json": '{"text":{"key":"原版界面"}}',
                "translations/ui/variants-source.json": '{"other":"原文变体"}',
                "app/src/main/assets/idoly-localify/dict.json": '{"font_mode":"replace-all"}',
            }
            for name, content in originals.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content, encoding="utf-8")
            self.assertTrue(replaces_all_text(root))
            dictionary = root / "app/src/main/assets/idoly-localify/dict.json"
            dictionary.write_text('{"font_mode":"replace-ui"}', encoding="utf-8")
            self.assertTrue(replaces_all_text(root))
            with patch.dict(os.environ, {"IDOLY_STORY_SOURCE_DIR": str(root.parent / "HoshimiToolkit/cache/resource/adventure")}):
                fields = list(source_fields(root))
            covered = {ord(character) for _, value in fields for character in value}
            report = audit_codepoints(covered, fields)
            self.assertEqual(report["missing_characters"], "")
            covered.remove(ord("标"))
            report = audit_codepoints(covered, fields)
            self.assertEqual(report["missing_characters"], "标")


if __name__ == "__main__":
    unittest.main()
