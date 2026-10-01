"""Historical dump audit distinguishes current matches from remaining candidates."""

import json
from pathlib import Path
import tempfile
import unittest

from audit_runtime_coverage import audit, textual_candidates, unmatched_bucket


class RuntimeCoverageAuditTests(unittest.TestCase):
    def test_path_scoped_activity_rule_is_not_claimed_covered_without_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / 'master'
            master.mkdir()
            (master / 'Story.json').write_text('{}')
            dictionary = root / 'dict.json'
            dictionary.write_text(json.dumps({'text': {}, 'i18n': {}, 'i18n_by_source': {},
                'translated_values': [], 'home_activity_names': {'サイン会': '签名会'}}))
            dump = root / 'dump.jsonl'
            dump.write_text(json.dumps({'kind': 'text', 'source': 'サイン会 Lv28'}) + '\n')
            report = audit(dump, dictionary, master, include_details=True)
            self.assertEqual(report['text_categories'], {'still_unmatched': 1})
            self.assertEqual(report['details']['text'][0]['path_scoped_candidate'], 'home_activity_names')

    def test_numeric_template_audit_uses_strict_ascii_full_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / 'master'
            master.mkdir()
            (master / 'Story.json').write_text('{}', encoding='utf-8')
            dictionary = root / 'dict.json'
            dictionary.write_text(json.dumps({
                'text': {}, 'i18n': {}, 'i18n_by_source': {},
                'translated_values': [],
                'numeric_text_templates': {
                    '{number}日目のログインボーナスです':
                    '这是第{number}天的登录奖励'}}, ensure_ascii=False), encoding='utf-8')
            dump = root / 'dump.jsonl'
            dump.write_text('\n'.join(json.dumps({'kind': 'text', 'source': value},
                ensure_ascii=False) for value in (
                '2日目のログインボーナスです', '8日目のログインボーナスです',
                '这是第8天的登录奖励', '２日目のログインボーナスです')) + '\n',
                encoding='utf-8')
            report = audit(dump, dictionary, master)
            self.assertEqual(report['text_categories'], {
                'now_numeric_template_source': 2,
                'now_rendered_numeric_template': 1,
                'still_unmatched': 1})

    def test_rechecks_historical_misses_without_claiming_whole_game_coverage(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / "master"
            master.mkdir()
            (master / "Story.json").write_text(json.dumps({
                "1": {"name": "旧原文"}, "2": {"name": "同形字"}}), encoding="utf-8")
            dictionary = root / "dict.json"
            dictionary.write_text(json.dumps({
                "text": {"旧原文": "新译文", "界面原文": "界面译文"},
                "i18n": {"stable.key": "稳定译文"},
                "i18n_by_source": {"variant.key": {"变体原文": "变体译文"}},
                "translated_values": ["已渲染译文"],
            }, ensure_ascii=False), encoding="utf-8")
            dump = root / "dump.jsonl"
            entries = [
                {"kind": "text", "source": "旧原文"},
                {"kind": "text", "source": "旧原文"},
                {"kind": "text", "source": "已渲染译文"},
                {"kind": "text", "source": "界面译文"},
                {"kind": "text", "source": "稳定译文"},
                {"kind": "text", "source": "变体译文"},
                {"kind": "text", "source": "12日前"},
                {"kind": "text", "source": "12天前"},
                {"kind": "text", "source": "同形字"},
                {"kind": "text", "source": "未译の30枚"},
                {"kind": "i18n", "key": "variant.key", "source": "变体原文"},
                {"kind": "i18n", "key": "stable.key", "source": "稳定原文"},
                {"kind": "i18n", "key": "other.key", "source": "界面原文"},
                {"kind": "i18n", "key": "missing.key", "source": "未知原文"},
            ]
            dump.write_text("\n".join(json.dumps(item, ensure_ascii=False)
                                       for item in entries) + "\n", encoding="utf-8")
            result = audit(dump, dictionary, master)
            self.assertEqual(result["scope"],
                             "historical_unmatched_dump_rechecked_against_current_dictionary")
            self.assertEqual(result["capture_records"], 14)
            self.assertEqual(result["text_categories"], {
                "master_source_without_change": 1, "now_exact_text": 1,
                "now_rendered_translation": 4, "now_dynamic_elapsed_source": 1,
                "now_rendered_dynamic_elapsed": 1, "still_unmatched": 1})
            self.assertEqual(result["still_unmatched_text_traits"], {
                "contains_digit": 1, "contains_kana": 1, "contains_newline": 0})
            self.assertEqual(result["still_unmatched_text_buckets"], {
                "kana_with_digit": 1})
            self.assertEqual(result["i18n_categories"], {
                "now_source_variant": 1, "now_stable_key": 1,
                "possible_text_fallback": 1, "unknown_key": 1})
            self.assertNotIn("details", result)

    def test_reviewed_same_source_is_not_counted_as_a_missing_translation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / "master"
            master.mkdir()
            (master / "Story.json").write_text("{}", encoding="utf-8")
            dictionary = root / "dict.json"
            dictionary.write_text(json.dumps({
                "text": {}, "i18n": {}, "i18n_by_source": {}, "translated_values": []}),
                encoding="utf-8")
            source = root / "source.json"
            source.write_text(json.dumps({"i18n": {
                "retained": "原文", "variant": "默认", "untranslated": "未译"}},
                ensure_ascii=False), encoding="utf-8")
            translation = root / "translation.json"
            translation.write_text(json.dumps({
                "i18n": {"retained": "原文"},
                "i18n_by_source": {"variant": {"另一种": "另一种"}}},
                ensure_ascii=False), encoding="utf-8")
            dump = root / "dump.jsonl"
            dump.write_text("\n".join(json.dumps({
                "kind": "i18n", "key": key, "source": value}, ensure_ascii=False)
                for key, value in (("retained", "原文"), ("variant", "另一种"),
                                   ("untranslated", "未译"))) + "\n", encoding="utf-8")
            result = audit(dump, dictionary, master, source, translation)
            self.assertEqual(result["i18n_categories"], {
                "reviewed_retained_key": 1, "reviewed_retained_variant": 1,
                "known_untranslated_key": 1})

    def test_rejects_corrupt_dump_line(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            master = root / "master"
            master.mkdir()
            (master / "Story.json").write_text("{}", encoding="utf-8")
            dictionary = root / "dict.json"
            dictionary.write_text(json.dumps({
                "text": {}, "i18n": {}, "i18n_by_source": {}, "translated_values": []}),
                encoding="utf-8")
            dump = root / "dump.jsonl"
            dump.write_text('{"kind":"text","source":', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "dump.jsonl:1"):
                audit(dump, dictionary, master)

    def test_local_review_candidates_exclude_numeric_and_latin_text(self):
        self.assertEqual(unmatched_bucket("30枚のチケット"), "kana_with_digit")
        self.assertEqual(unmatched_bucket("限定開催"), "han_without_digit")
        self.assertEqual(unmatched_bucket("360LIVE"), "latin_or_symbol_with_digit")
        report = {"details": {"text": [
            {"source": "30枚のチケット", "category": "still_unmatched",
             "bucket": "kana_with_digit", "occurrences": 2},
            {"source": "限定開催", "category": "still_unmatched",
             "bucket": "han_without_digit", "occurrences": 1},
            {"source": "360LIVE", "category": "still_unmatched",
             "bucket": "latin_or_symbol_with_digit", "occurrences": 1},
            {"source": "旧原文", "category": "master_source_without_change",
             "occurrences": 1},
        ]}}
        self.assertEqual([item["source"] for item in textual_candidates(report)],
                         ["30枚のチケット", "限定開催"])


if __name__ == "__main__":
    unittest.main()
