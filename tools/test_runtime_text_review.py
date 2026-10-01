"""Runtime Text enters reviewed dictionaries only after explicit selection."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import runtime_text_review
from localize import build


class RuntimeTextReviewTests(unittest.TestCase):
    def test_selected_text_only_and_manual_edits_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            analysis = root / ".analysis"
            analysis.mkdir()
            candidates = analysis / "candidates.json"
            review = analysis / "review.json"
            source = root / "source.json"
            translations = root / "zh-Hans.json"
            candidates.write_text(json.dumps({
                "scope": "local_review_only_no_automatic_translation",
                "audit_schema_version": 2,
                "texts": [
                    {"source": "読み込み中", "occurrences": 2,
                     "category": "still_unmatched", "bucket": "kana_without_digit"},
                    {"source": "保留原文", "occurrences": 1,
                     "category": "still_unmatched", "bucket": "han_without_digit"},
                    {"source": "玩家名", "occurrences": 1,
                     "category": "still_unmatched", "bucket": "han_without_digit"},
                ],
            }, ensure_ascii=False), encoding="utf-8")
            source.write_text('{"text": {}, "i18n": {}}', encoding="utf-8")
            translations.write_text('{"text": {}, "i18n": {}}', encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                self.assertEqual(runtime_text_review.prepare(candidates, review), 3)
            plan = json.loads(review.read_text(encoding="utf-8"))
            plan["entries"][0].update(decision="translate", translation="加载中")
            plan["entries"][1]["decision"] = "retain"
            review.write_text(json.dumps(plan, ensure_ascii=False), encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                self.assertEqual(runtime_text_review.apply_review(
                    candidates, review, source, translations), {
                        "selected": 2, "added_sources": 2,
                        "added_translations": 2, "preserved_translations": 0})
            self.assertEqual(json.loads(source.read_text(encoding="utf-8"))["text"], {
                "読み込み中": "読み込み中", "保留原文": "保留原文"})
            self.assertEqual(json.loads(translations.read_text(encoding="utf-8"))["text"], {
                "読み込み中": "加载中", "保留原文": "保留原文"})
            self.assertNotIn("玩家名", source.read_text(encoding="utf-8"))
            dictionary = build(source, translations, root / "dict.json",
                               dump=False, font=False)
            self.assertEqual(dictionary["text"], {"読み込み中": "加载中"})
            translated = json.loads(translations.read_text(encoding="utf-8"))
            translated["text"]["読み込み中"] = "正在加载"
            translations.write_text(json.dumps(translated, ensure_ascii=False), encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                result = runtime_text_review.apply_review(candidates, review, source, translations)
            self.assertEqual(result["added_translations"], 0)
            self.assertEqual(json.loads(translations.read_text(encoding="utf-8"))
                             ["text"]["読み込み中"], "正在加载")

    def test_review_rejects_changed_candidate_list(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            analysis = root / ".analysis"
            analysis.mkdir()
            candidates = analysis / "candidates.json"
            review = analysis / "review.json"
            candidates.write_text(json.dumps({
                "scope": "local_review_only_no_automatic_translation",
                "audit_schema_version": 2,
                "texts": [{"source": "固定文字", "occurrences": 1,
                           "category": "still_unmatched", "bucket": "han_without_digit"}],
            }, ensure_ascii=False), encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                runtime_text_review.prepare(candidates, review)
            candidates.write_text(candidates.read_text(encoding="utf-8") + "\n",
                                  encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                with self.assertRaisesRegex(ValueError, "stale"):
                    runtime_text_review.selected_entries(candidates, review)

    def test_manual_translation_preserves_line_breaks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            analysis = root / ".analysis"
            analysis.mkdir()
            candidates = analysis / "candidates.json"
            review = analysis / "review.json"
            candidates.write_text(json.dumps({
                "scope": "local_review_only_no_automatic_translation",
                "audit_schema_version": 2,
                "texts": [{"source": "一行\n二行", "occurrences": 1,
                           "category": "still_unmatched", "bucket": "han_without_digit"}],
            }, ensure_ascii=False), encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                runtime_text_review.prepare(candidates, review)
            plan = json.loads(review.read_text(encoding="utf-8"))
            plan["entries"][0].update(decision="translate", translation="一行二行")
            review.write_text(json.dumps(plan, ensure_ascii=False), encoding="utf-8")
            with patch.object(runtime_text_review, "ROOT", root):
                with self.assertRaisesRegex(ValueError, "line break"):
                    runtime_text_review.selected_entries(candidates, review)

    def test_refresh_keeps_exact_source_decisions_and_archives_old_review(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            analysis = root / ".analysis"
            analysis.mkdir()
            candidates = analysis / "candidates.json"
            review = analysis / "review.json"

            def write_candidates(rows):
                candidates.write_text(json.dumps({
                    "scope": "local_review_only_no_automatic_translation",
                    "audit_schema_version": 2,
                    "texts": [{"source": value, "occurrences": count,
                               "category": "still_unmatched", "bucket": "han_without_digit"}
                              for value, count in rows],
                }, ensure_ascii=False), encoding="utf-8")

            write_candidates([("固定甲", 2), ("固定乙", 1)])
            with patch.object(runtime_text_review, "ROOT", root):
                runtime_text_review.prepare(candidates, review)
            old = json.loads(review.read_text(encoding="utf-8"))
            old["entries"][0].update(decision="translate", translation="固定A")
            old["entries"][1]["decision"] = "retain"
            review.write_text(json.dumps(old, ensure_ascii=False), encoding="utf-8")
            old_bytes = review.read_bytes()
            write_candidates([("固定甲", 5), ("固定丙", 1)])
            with patch.object(runtime_text_review, "ROOT", root):
                result = runtime_text_review.refresh(candidates, review)
                selected = runtime_text_review.selected_entries(candidates, review)
            self.assertEqual({key: result[key] for key in (
                "candidates", "preserved_selected", "new_candidates", "removed_selected")}, {
                    "candidates": 2, "preserved_selected": 1,
                    "new_candidates": 1, "removed_selected": 1})
            self.assertEqual(Path(result["archive"]).read_bytes(), old_bytes)
            self.assertEqual([(item["source"], item["translation"])
                              for item in selected], [("固定甲", "固定A")])
            self.assertEqual(json.loads(review.read_text(encoding="utf-8"))
                             ["entries"][1]["decision"], "skip")


if __name__ == "__main__":
    unittest.main()
