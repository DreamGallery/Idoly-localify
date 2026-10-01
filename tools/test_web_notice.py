import json
from pathlib import Path
import tempfile
import unittest

from web_notice import build, headline_aliases, import_pages, import_snapshot, notice_path


PATH = "/notice/" + "a" * 64 + "/index.html"


class WebNoticeWorkflowTest(unittest.TestCase):
    def test_title_conflicts_require_bound_review_and_keep_page_translations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, reviewed, report = [root / name for name in
                                       ("source.json", "reviewed.json", "report.json")]
            other = PATH.replace("a" * 64, "b" * 64)
            pages = {path: {"title": "notice", "headline": "開催", "texts": ["開催"]}
                     for path in (PATH, other)}
            source.write_text(json.dumps({"pages": pages}))
            compiled = {"pages": {PATH: {"開催": "开启"}, other: {"開催": "举办"}}}
            before = json.dumps(compiled)
            self.assertEqual(headline_aliases(source, compiled, conflict_report=report), [])
            self.assertEqual(json.loads(report.read_text())["unresolved_titles"], 1)
            decision = {"schema_version": 1, "resolutions": {"開催": {
                "translation": "开启", "review_method": "assistant", "reason": "Retain reviewed label",
                "candidates": {PATH: "开启", other: "举办"}}}}
            reviewed.write_text(json.dumps(decision))
            self.assertEqual(headline_aliases(source, compiled, reviewed, report), [("開催", "开启")])
            self.assertEqual(json.loads(report.read_text())["unresolved_titles"], 0)
            self.assertEqual(json.dumps(compiled), before)
            compiled["pages"][other]["開催"] = "举行"
            with self.assertRaisesRegex(ValueError, "Stale or invalid"):
                headline_aliases(source, compiled, reviewed)
            decision["resolutions"]["開催"]["candidates"][other] = "举行"
            decision["resolutions"]["開催"]["translation"] = "未审阅的新值"
            reviewed.write_text(json.dumps(decision))
            with self.assertRaisesRegex(ValueError, "Stale or invalid"):
                headline_aliases(source, compiled, reviewed)

    def test_identical_title_translations_export_once_and_ignore_page_order(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.json"
            paths = [PATH, PATH.replace("a" * 64, "b" * 64)]
            compiled = {"pages": {path: {"開催": "开启"} for path in paths}}
            for order in (paths, list(reversed(paths))):
                source.write_text(json.dumps({"pages": {path: {
                    "title": "notice", "headline": "開催", "entry_titles": ["開催"],
                    "texts": ["開催"]} for path in order}}))
                self.assertEqual(headline_aliases(source, compiled), [("開催", "开启")])

    def test_batch_import_preserves_history_and_separate_list_title(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, incoming, index = [root / name for name in ('source.json', 'new.json', 'index.json')]
            old_path = PATH.replace('a' * 64, 'b' * 64)
            source.write_text(json.dumps({'pages': {old_path: {'title': 'old', 'texts': ['旧公告']}}}))
            incoming.write_text(json.dumps({'pages': {PATH: {'title': 'notice', 'headline': '見出し',
                                                          'texts': ['見出し', '本文']}}}))
            index.write_text(json.dumps({'notices': [{'linkDetail': 'https://stat.game-idolypride.jp' + PATH,
                                                    'title': '予告', 'listTitle': '短い予告'}]}))
            import_pages(incoming, source, index)
            before = source.read_bytes()
            import_pages(incoming, source, index)
            self.assertEqual(before, source.read_bytes())
            pages = json.loads(before)['pages']
            self.assertIn(old_path, pages)
            self.assertEqual(pages[PATH]['entry_titles'], ['予告', '短い予告'])
            self.assertEqual(headline_aliases(source, {'pages': {PATH: {'短い予告': '简短预告'}}}),
                             [('短い予告', '简短预告')])
            index.write_text(json.dumps({'notices': [{'linkDetail': 'https://evil.invalid' + PATH}]}))
            with self.assertRaisesRegex(ValueError, 'nonofficial'):
                import_pages(incoming, source, index)
            self.assertEqual(source.read_bytes(), before)

    def test_import_is_idempotent_and_build_uses_ui_title(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            snapshot = root / "snapshot.json"
            source = root / "source.json"
            translation = root / "zh-Hans.json"
            ui = root / "ui.json"
            output = root / "compiled.json"
            snapshot.write_text(json.dumps({
                "path": PATH, "title": "公告", "nodes": [
                    {"tag": "H1", "text": "活动开始！"},
                    {"tag": "P", "text": "说明正文"},
                    {"tag": "STRONG", "text": "说明正文"},
                ],
            }, ensure_ascii=False), encoding="utf-8")
            self.assertEqual(import_snapshot(snapshot, source), (PATH, 2))
            self.assertEqual(json.loads(source.read_text(encoding="utf-8"))
                             ["pages"][PATH]["headline"], "活动开始！")
            before = source.read_bytes()
            self.assertEqual(import_snapshot(snapshot, source), (PATH, 0))
            self.assertEqual(source.read_bytes(), before)
            translation.write_text(json.dumps({"pages": {PATH: {"说明正文": "正文说明"}}},
                                              ensure_ascii=False), encoding="utf-8")
            ui.write_text(json.dumps({"text": {"活动开始！": "活动开启！"}},
                                       ensure_ascii=False), encoding="utf-8")
            result = build(source, translation, ui, output)
            self.assertEqual(result["pages"][PATH],
                             {"活动开始！": "活动开启！", "说明正文": "正文说明"})
            self.assertEqual(json.loads(output.read_text(encoding="utf-8")), result)
            self.assertEqual(headline_aliases(source, result),
                             [("活动开始！", "活动开启！")])

    def test_explicit_blank_translation_suppresses_ui_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            translation = root / "zh-Hans.json"
            ui = root / "ui.json"
            source.write_text(json.dumps({"pages": {PATH: {
                "title": "公告", "texts": ["活動"]}}}, ensure_ascii=False), encoding="utf-8")
            translation.write_text(json.dumps({"pages": {PATH: {"活動": ""}}},
                                              ensure_ascii=False), encoding="utf-8")
            ui.write_text(json.dumps({"text": {"活動": "活动"}},
                                       ensure_ascii=False), encoding="utf-8")
            result = build(source, translation, ui, root / "compiled.json")
            self.assertEqual(result["pages"], {})

    def test_rejects_unknown_source_and_non_notice_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.json"
            translation = root / "zh-Hans.json"
            ui = root / "ui.json"
            source.write_text(json.dumps({"pages": {PATH: {
                "title": "公告", "texts": ["原文"]}}}, ensure_ascii=False), encoding="utf-8")
            translation.write_text(json.dumps({"pages": {PATH: {"别的文字": "译文"}}},
                                              ensure_ascii=False), encoding="utf-8")
            ui.write_text('{"text":{}}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "not in source page"):
                build(source, translation, ui, root / "compiled.json")
            with self.assertRaisesRegex(ValueError, "Invalid IDOLY PRIDE notice path"):
                notice_path("/account/profile")


if __name__ == "__main__":
    unittest.main()
