"""Only reviewed event mission numbers may be generalized at runtime."""
import json
from pathlib import Path
import tempfile
import unittest

from mission_numeric_templates import derive, merge
from numeric_text_templates import match_template
from localize import build


class MissionNumericTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "source.json"
        self.target = self.root / "target.json"
        self.original = "フォト撮影{threshold}回"
        self.translation = "拍照{threshold}次"

    def write(self, source, target):
        self.source.write_text(json.dumps(source), encoding="utf-8")
        self.target.write_text(json.dumps(target), encoding="utf-8")

    def test_scoped_records_are_deduplicated_and_have_source_provenance(self):
        keys = ["event-love-future-character", "event-ms-idoly-pride-day-future-date"]
        source = {key: {"name": self.original} for key in keys}
        target = {key: {"name": self.translation} for key in keys}
        source["unrelated-mission"] = {"name": "未知{threshold}文字"}
        self.write(source, target)
        templates, origins = derive(self.source, self.target)
        self.assertEqual(templates, {"フォト撮影{number}回": "拍照{number}次"})
        self.assertEqual(origins["フォト撮影{number}回"], [f"Mission/{key}/name" for key in keys])
        self.assertTrue(match_template("フォト撮影12回", "フォト撮影{number}回"))
        for invalid in ["フォト撮影１２回", "フォト撮影1234567回", "フォト撮影麻奈回", "前フォト撮影12回"]:
            self.assertFalse(match_template(invalid, "フォト撮影{number}回"))

    def test_missing_table_pair_is_rejected_but_empty_fixture_is_allowed(self):
        self.assertEqual(derive(self.source, self.target), ({}, {}))
        self.source.write_text("{}")
        with self.assertRaisesRegex(ValueError, "both source and translation"):
            derive(self.source, self.target)

    def test_missing_conflicting_and_multiple_placeholder_translations_fail(self):
        key = "event-love-new"
        cases = [
            ({key: {"name": self.original}}, {}, "missing mission translation"),
            ({key: {"name": self.original}}, {key: {"name": "拍照一次"}}, "exactly one"),
            ({key: {"name": "{user}のフォト撮影{threshold}回"}},
             {key: {"name": "{user}拍照{threshold}次"}}, "other placeholders"),
            ({key: {"name": self.original + "{threshold}"}},
             {key: {"name": self.translation}}, "exactly one"),
            ({key: {"name": self.original}, key + "2": {"name": self.original}},
             {key: {"name": self.translation}, key + "2": {"name": "摄影{threshold}次"}}, "conflicting"),
        ]
        for source, target, message in cases:
            with self.subTest(message=message):
                self.write(source, target)
                with self.assertRaisesRegex(ValueError, message):
                    derive(self.source, self.target)

    def test_manual_duplicates_conflicts_and_combined_limit(self):
        values = {"フォト撮影{number}回": "拍照{number}次"}
        self.assertEqual(merge(values, values), values)
        with self.assertRaisesRegex(ValueError, "Conflicting manual"):
            merge(values, {"フォト撮影{number}回": "摄影{number}次"})
        manual = {f"テスト番号{i}番{{number}}回": f"测试{i}号{{number}}次" for i in range(128)}
        with self.assertRaisesRegex(ValueError, "at most 128"):
            merge(manual, values)

    def test_localize_merges_templates_without_shipping_provenance_and_preserves_output_on_failure(self):
        self.write({"event-love-new": {"name": self.original}},
                   {"event-love-new": {"name": self.translation}})
        ui_source, ui_target, output, report = [self.root / value for value in
            ["ui-source.json", "ui-target.json", "dict.json", "sources.json"]]
        ui_source.write_text('{"i18n":{},"text":{}}')
        ui_target.write_text('{"i18n":{},"text":{}}')
        result = build(ui_source, ui_target, output, False,
                       mission_source=self.source, mission_translation=self.target,
                       mission_template_report=report)
        self.assertEqual(result["numeric_text_templates"], {"フォト撮影{number}回": "拍照{number}次"})
        self.assertNotIn("numeric_text_template_sources", result)
        self.assertEqual(json.loads(report.read_text())["sources"]["フォト撮影{number}回"],
                         ["Mission/event-love-new/name"])
        before = output.read_bytes(), report.read_bytes()
        self.target.write_text("{}")
        with self.assertRaisesRegex(ValueError, "missing mission translation"):
            build(ui_source, ui_target, output, False,
                  mission_source=self.source, mission_translation=self.target,
                  mission_template_report=report)
        self.assertEqual((output.read_bytes(), report.read_bytes()), before)


if __name__ == "__main__":
    unittest.main()
