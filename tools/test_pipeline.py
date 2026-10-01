"""Guarded text build: repeatability, drift detection, and no partial updates."""

import csv
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from pipeline import Pipeline, load_toolkit
import pipeline as pipeline_module

from workspace_paths import toolkit_root

PROJECT_ROOT = Path(__file__).resolve().parents[1]


class ToolkitBoundaryTests(unittest.TestCase):
    def test_missing_toolkit_has_setup_message(self):
        missing = ModuleNotFoundError("No module named HoshimiToolkit",
                                      name="HoshimiToolkit")
        with patch("pipeline.importlib.import_module", side_effect=missing):
            with self.assertRaisesRegex(ValueError, "Clone its repository"):
                load_toolkit()

    def test_incompatible_toolkit_version_is_reported(self):
        with patch("pipeline.importlib.import_module", return_value=SimpleNamespace(
                WORKFLOW_API_VERSION=99)):
            with self.assertRaisesRegex(ValueError, "workflow API 99"):
                load_toolkit()


def put_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")


@unittest.skipUnless((toolkit_root() / "src/adv_csv.py").is_file(),
                     "The separately cloned HoshimiToolkit project is required")
class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "localify"
        source = self.root.parent / "HoshimiToolkit/cache/resource/adventure/adv_test.txt"
        paths = patch.dict(os.environ, {
            "IDOLY_STORY_SOURCE_DIR": str(source.parent),
            "IDOLY_STORY_CSV_DIR": str(self.root.parent / "HoshimiToolkit/cache/csv"),
            "IDOLY_STORY_OUTPUT_DIR": str(self.root.parent / "HoshimiToolkit/cache/local-files/resource"),
        })
        paths.start()
        self.addCleanup(paths.stop)
        source.parent.mkdir(parents=True)
        source.write_text("[title title=第一話]\n[message text=こんにちは name=麻奈]\n",
                          encoding="utf-8")
        put_json(self.root / "translations/glossary/names.json", {"麻奈": "麻奈"})
        put_json(self.root / "translations/glossary/terms.json", {})
        put_json(self.root / "translations/ui/source.json", {"text": {}, "i18n": {}})
        put_json(self.root / "translations/ui/zh-Hans.json", {"text": {}, "i18n": {}})
        put_json(self.root / "translations/notice/source.json", {"pages": {}})
        put_json(self.root / "translations/notice/zh-Hans.json", {"pages": {}})
        put_json(self.root / "translations/master/orig/Story.json",
                 {"one": {"name": "第一話"}})
        put_json(self.root / "translations/master/zh-Hans/Story.json",
                 {"one": {"name": "第一话"}})
        put_json(self.root / "translations/master/orig/Card.json", {"card-one": {}})
        put_json(self.root / "translations/master/zh-Hans/Card.json", {"card-one": {}})
        put_json(self.root / "translations/story-metadata/catalog.json", {
            "schema_version": 1,
            "stories": {"one": {"name": "第一話", "categories": ["card"],
                                "assets": ["card_test_01"],
                                "references": [{"category": "card", "card_id": "card-one",
                                                "episode": 1}]}}})
        self.pipeline = Pipeline(self.root)
        self.assertEqual(self.pipeline.export(), 1)
        self.csv_path = self.pipeline.csv_dir / "adv_test.csv"
        self.set_translation("こんにちは", "你好")

    def set_translation(self, original: str, target: str) -> None:
        with self.csv_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        for row in rows:
            if row["text"] == original:
                row["trans"] = target
        with self.csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"],
                                    lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)

    def test_mission_templates_and_provenance_are_published_and_verified(self):
        put_json(self.root / "translations/master/orig/Mission.json", {
            "event-love-new-character": {"name": "フォト撮影{threshold}回"}})
        put_json(self.root / "translations/master/zh-Hans/Mission.json", {
            "event-love-new-character": {"name": "拍照{threshold}次"}})
        self.pipeline.run()
        compiled = json.loads(self.pipeline.dictionary.read_text(encoding="utf-8"))
        self.assertEqual(compiled["numeric_text_templates"], {
            "フォト撮影{number}回": "拍照{number}次"})
        report = json.loads(self.pipeline.mission_template_sources.read_text(encoding="utf-8"))
        self.assertEqual(report["sources"]["フォト撮影{number}回"],
                         ["Mission/event-love-new-character/name"])
        self.pipeline.run(verify=True)
        before = self.pipeline.dictionary.read_bytes()
        self.pipeline.mission_template_sources.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "mission-template-sources"):
            self.pipeline.run(verify=True)
        self.assertEqual(self.pipeline.dictionary.read_bytes(), before)

    def test_build_verify_and_incremental_rebuild(self):
        first = self.pipeline.run()
        self.assertEqual(first["rebuilt"], 1)
        self.assertEqual(first["master_keyed_fields"], 1)
        self.assertEqual(json.loads(self.pipeline.master_keyed.read_text(
            encoding="utf-8"))["tables"]["Story"]["one"]["name"],
            ["第一話", "第一话"])
        self.assertEqual(first["runtime_conflicts"], 0)
        self.assertEqual(json.loads(self.pipeline.runtime_conflicts.read_text(
            encoding="utf-8")), {"conflicts": []})
        self.assertEqual(self.pipeline.run(verify=True)["rebuilt"], 0)
        self.assertEqual(self.pipeline.run()["rebuilt"], 0)
        output = self.pipeline.txt_dir / "adv_test.txt"
        self.assertIn("text=你好", output.read_text(encoding="utf-8"))
        self.set_translation("こんにちは", "您好")
        with self.assertRaisesRegex(ValueError, "1 story outputs or inputs changed"):
            self.pipeline.run(verify=True)
        self.assertEqual(self.pipeline.run()["rebuilt"], 1)
        self.assertIn("text=您好", output.read_text(encoding="utf-8"))
        self.assertEqual(self.pipeline.run(verify=True)["rebuilt"], 0)

    def test_conflict_report_is_built_and_verified(self):
        put_json(self.root / "translations/ui/zh-Hans.json",
                 {"text": {"第一話": "首话"}, "i18n": {}})
        result = self.pipeline.run()
        self.assertEqual(result["runtime_conflicts"], 1)
        report = json.loads(self.pipeline.runtime_conflicts.read_text(encoding="utf-8"))
        self.assertEqual(report["conflicts"][0]["selected"],
                         {"origin": "ui-text", "translation": "首话"})
        self.assertEqual(report["conflicts"][0]["discarded"],
                         [{"origin": "master-text", "translation": "第一话"}])
        self.pipeline.run(verify=True)
        self.pipeline.runtime_conflicts.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Compiled artifacts differ"):
            self.pipeline.run(verify=True)

    def test_notice_title_review_is_bound_and_reported_without_changing_pages(self):
        paths = ["/notice/" + character * 64 + "/index.html" for character in ("a", "b")]
        put_json(self.pipeline.web_notice_source, {"pages": {path: {
            "title": "notice", "headline": "開催", "texts": ["開催"]} for path in paths}})
        put_json(self.pipeline.web_notice_translation, {"pages": {
            paths[0]: {"開催": "开启"}, paths[1]: {"開催": "举办"}}})
        self.pipeline.run()
        self.assertEqual(pipeline_module.json_file(self.pipeline.web_notice_title_conflicts)["unresolved_titles"], 1)
        put_json(self.pipeline.web_notice_title_resolutions, {"schema_version": 1,
            "resolutions": {"開催": {"translation": "开启", "review_method": "assistant",
                "reason": "Reviewed existing label", "candidates": dict(zip(paths, ["开启", "举办"]))}}})
        self.pipeline.run()
        self.assertEqual(pipeline_module.json_file(self.pipeline.web_notice_title_conflicts)["unresolved_titles"], 0)
        self.assertEqual(pipeline_module.json_file(self.pipeline.web_notice_dictionary)["pages"][paths[1]]["開催"], "举办")
        before = self.pipeline.dictionary.read_bytes()
        put_json(self.pipeline.web_notice_translation, {"pages": {
            paths[0]: {"開催": "开启"}, paths[1]: {"開催": "举行"}}})
        with self.assertRaisesRegex(ValueError, "Stale or invalid notice title resolution"):
            self.pipeline.run()
        self.assertEqual(self.pipeline.dictionary.read_bytes(), before)

    def test_status_reports_build_then_ready_without_modifying_review_inputs(self):
        before = self.csv_path.read_bytes()
        self.assertEqual(self.pipeline.status()["next_command"],
                         "python3 tools/pipeline.py build")
        self.assertEqual(self.csv_path.read_bytes(), before)
        self.pipeline.run()
        self.assertEqual(pipeline_module.json_file(self.pipeline.dictionary)["font_mode"],
                         "replace-ui")
        status = self.pipeline.status()
        self.assertTrue(status["ready"])
        self.assertIsNone(status["next_command"])

    def test_status_distinguishes_new_and_revised_story_sources(self):
        self.pipeline.run()
        source = self.pipeline.source_dir / "adv_test.txt"
        source.write_text(source.read_text(encoding="utf-8") + "[timeline]\n",
                          encoding="utf-8")
        status = self.pipeline.status()
        self.assertEqual(status["stale_csv"], ["adv_test"])
        self.assertEqual(status["next_command"],
                         "python3 tools/pipeline.py refresh")
        (self.pipeline.source_dir / "adv_new.txt").write_text(
            "[message text=新しい話]\n", encoding="utf-8")
        status = self.pipeline.status()
        self.assertEqual(status["missing_csv"], ["adv_new"])
        self.assertEqual(status["next_command"],
                         "python3 tools/pipeline.py export")

    def test_status_does_not_recommend_build_for_invalid_edited_csv(self):
        self.pipeline.run()
        self.set_translation("こんにちは", "[无效]")
        status = self.pipeline.status()
        self.assertFalse(status["ready"])
        self.assertIsNone(status["next_command"])
        self.assertIn("Review changed CSV", status["issue"])

    def test_status_rejects_corrupt_source_metadata_before_refresh(self):
        with self.csv_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        rows[-2]["text"] = "broken-checksum"
        with self.csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"],
                                    lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        status = self.pipeline.status()
        self.assertEqual(status["invalid_csv"], ["adv_test"])
        self.assertIsNone(status["next_command"])

    def test_rejects_stale_source_without_touching_outputs(self):
        self.pipeline.run()
        output = self.pipeline.txt_dir / "adv_test.txt"
        before = output.read_bytes()
        source = self.pipeline.source_dir / "adv_test.txt"
        source.write_text(source.read_text(encoding="utf-8") + "[timeline]\n",
                          encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "different script or revision"):
            self.pipeline.run()
        self.assertEqual(output.read_bytes(), before)

    def test_detects_compiled_dictionary_drift(self):
        self.pipeline.run()
        self.pipeline.dictionary.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Compiled artifacts differ"):
            self.pipeline.run(verify=True)
        self.assertEqual(self.pipeline.run()["artifacts"], 1)
        self.assertEqual(self.pipeline.run(verify=True)["artifacts"], 0)

    def test_detects_stale_master_keyed_index(self):
        self.pipeline.run()
        self.pipeline.master_keyed.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "master-keyed.json"):
            self.pipeline.run(verify=True)
        self.assertEqual(self.pipeline.run()["artifacts"], 1)
        self.assertEqual(self.pipeline.run(verify=True)["artifacts"], 0)

    def test_web_notice_is_built_and_verified_with_other_text(self):
        notice_path = "/notice/" + "a" * 64 + "/index.html"
        put_json(self.pipeline.web_notice_source, {"pages": {
            notice_path: {"title": "公告", "texts": ["活動開始"]}}})
        put_json(self.pipeline.web_notice_translation, {"pages": {
            notice_path: {"活動開始": "活动开始"}}})
        self.assertEqual(self.pipeline.run()["web_notice"], 1)
        compiled = json.loads(self.pipeline.web_notice_dictionary.read_text(encoding="utf-8"))
        self.assertEqual(compiled["pages"][notice_path]["活動開始"], "活动开始")
        self.pipeline.run(verify=True)
        self.pipeline.web_notice_dictionary.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Compiled artifacts differ"):
            self.pipeline.run(verify=True)

    def test_notice_headline_becomes_runtime_list_title_without_ui_entry(self):
        notice_path = "/notice/" + "b" * 64 + "/index.html"
        headline = "【予告】おでかけミッション開催！"
        put_json(self.pipeline.web_notice_source, {"pages": {
            notice_path: {"title": "公告", "headline": headline,
                          "texts": [headline, "本文"]}}})
        put_json(self.pipeline.web_notice_translation, {"pages": {
            notice_path: {headline: "【预告】外出任务即将开启！", "本文": "正文"}}})
        self.pipeline.run()
        compiled = json.loads(self.pipeline.dictionary.read_text(encoding="utf-8"))
        self.assertEqual(compiled["text"][headline], "【预告】外出任务即将开启！")
        self.assertEqual(compiled["text"]["おでかけミッション開催！"],
                         "外出任务即将开启！")
        self.pipeline.run(verify=True)

    def test_build_does_not_publish_when_csv_changes_while_staging(self):
        self.pipeline.run()
        self.set_translation("こんにちは", "您好")
        output = self.pipeline.txt_dir / "adv_test.txt"
        manifest = self.pipeline.manifest
        before = (output.read_bytes(), manifest.read_bytes())
        real_build = pipeline_module.localize.build

        def edit_during_build(*args, **kwargs):
            result = real_build(*args, **kwargs)
            self.set_translation("こんにちは", "晚安")
            return result

        with patch.object(pipeline_module.localize, "build", edit_during_build):
            with self.assertRaisesRegex(ValueError, "inputs changed during build"):
                self.pipeline.run()
        self.assertEqual((output.read_bytes(), manifest.read_bytes()), before)
        self.assertEqual(self.pipeline.run()["rebuilt"], 1)
        self.assertIn("text=晚安", output.read_text(encoding="utf-8"))

    def test_build_does_not_overwrite_compiled_output_changed_while_staging(self):
        self.pipeline.run()
        dictionary = self.pipeline.dictionary
        manifest = self.pipeline.manifest
        before_manifest = manifest.read_bytes()
        real_build = pipeline_module.localize.build

        def edit_during_build(*args, **kwargs):
            result = real_build(*args, **kwargs)
            dictionary.write_text('{"manual": "edit"}', encoding="utf-8")
            return result

        with patch.object(pipeline_module.localize, "build", edit_during_build):
            with self.assertRaisesRegex(ValueError, "outputs changed during build"):
                self.pipeline.run()
        self.assertEqual(dictionary.read_text(encoding="utf-8"),
                         '{"manual": "edit"}')
        self.assertEqual(manifest.read_bytes(), before_manifest)

    def test_interrupted_publish_is_repaired_by_next_build(self):
        self.pipeline.run()
        self.set_translation("こんにちは", "您好")
        previous_manifest = self.pipeline.manifest.read_bytes()
        real_copy = pipeline_module.atomic_copy
        copies = 0

        def interrupt_second_copy(source, target):
            nonlocal copies
            copies += 1
            if copies == 2:
                raise OSError("simulated publish interruption")
            real_copy(source, target)

        with patch.object(pipeline_module, "atomic_copy", interrupt_second_copy):
            with self.assertRaisesRegex(OSError, "simulated publish interruption"):
                self.pipeline.run()
        self.assertEqual(self.pipeline.manifest.read_bytes(), previous_manifest)
        with self.assertRaisesRegex(ValueError, "story outputs or inputs changed"):
            self.pipeline.run(verify=True)
        self.assertEqual(self.pipeline.run()["rebuilt"], 1)
        self.assertIn("text=您好", (self.pipeline.txt_dir / "adv_test.txt").read_text())
        self.assertEqual(self.pipeline.run(verify=True)["rebuilt"], 0)

    def test_master_translation_must_match_source(self):
        put_json(self.root / "translations/master/zh-Hans/Story.json",
                 {"one": {"name": "第一话", "other": "错误"}})
        with self.assertRaisesRegex(ValueError, "translation has no source field"):
            self.pipeline.run()

    def test_missing_master_translation_table_is_rejected(self):
        put_json(self.root / "translations/master/orig/Other.json",
                 {"one": {"name": "別名"}})
        put_json(self.root / "translations/master/zh-Hans/Other.json", {})
        (self.root / "translations/master/zh-Hans/Story.json").unlink()
        with self.assertRaisesRegex(ValueError, "Missing MasterDB translation table"):
            self.pipeline.run()

    def test_missing_card_catalog_is_rejected_without_publishing(self):
        catalog = self.root / "translations/story-metadata/catalog.json"
        catalog.unlink()
        with self.assertRaisesRegex(ValueError, "Missing translation input: .*story-metadata/catalog.json"):
            self.pipeline.run()
        self.assertFalse(self.pipeline.dictionary.exists())

    def legacy_story(self):
        source = self.pipeline.source_dir / "adv_test.txt"
        source.write_text("[narration text=風景]\n[choicegroup text=はい text=いいえ]\n"
                          "[message text=こんにちは name=麻奈]\n", encoding="utf-8")
        toolkit = load_toolkit()
        toolkit.export_file(source, self.csv_path)
        with self.csv_path.open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        for row, item in zip(rows[:-2], [item for item in toolkit.fields(source.read_text())
                                        if item.key != "name"]):
            row["id"] = item.identifier
            row["trans"] = {"風景": "风景", "はい": "是", "いいえ": "否", "こんにちは": "你好"}[row["text"]]
        rows[-2]["trans"] = "source metadata note"
        rows[-1].update(name="reviewer", text="署名信息", trans="footer note")
        self.write_story_rows(rows)
        return rows

    def write_story_rows(self, rows):
        with self.csv_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"],
                                    lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)

    def test_refresh_migrates_same_hash_legacy_ids_without_changing_values(self):
        before_rows = self.legacy_story()
        before_bytes = self.csv_path.read_bytes()
        result = self.pipeline.refresh()
        self.assertEqual(result, {"scripts": 1, "preserved": 4, "unmatched": 0})
        with self.csv_path.open(encoding="utf-8", newline="") as stream:
            actual = list(csv.DictReader(stream))
        expected = [dict(row) for row in before_rows]
        for row, identifier in zip(expected, ["1:narration:1", "2:choice:1", "2:choice:2", "3:text:1"]):
            row["id"] = identifier
        self.assertEqual(actual, expected)
        report = json.loads((self.root / ".analysis/rebase-report.json").read_text())
        self.assertEqual(report["scripts"]["adv_test"]["migrated_ids"], 3)
        backups = list((self.root / ".analysis/rebase-backup").glob("*.csv"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_bytes(), before_bytes)
        migrated_bytes = self.csv_path.read_bytes()
        self.assertEqual(self.pipeline.refresh()["scripts"], 0)
        self.assertEqual(self.csv_path.read_bytes(), migrated_bytes)

    def test_same_hash_migration_rejects_changed_source_name_or_unrelated_alias(self):
        rows = self.legacy_story()
        for column, value in (("text", "偽の原文"), ("name", "違う話者"),
                              ("id", "1:choice:1"), ("id", "99:text:1")):
            with self.subTest(column=column, value=value):
                changed = [dict(row) for row in rows]
                changed[0][column] = value
                self.write_story_rows(changed)
                before = self.csv_path.read_bytes()
                with self.assertRaisesRegex(ValueError, "CSV source mismatch"):
                    self.pipeline.refresh()
                self.assertEqual(self.csv_path.read_bytes(), before)
        self.assertFalse((self.root / ".analysis/rebase-backup").exists())

    def test_same_hash_migration_rejects_source_filename_and_footer_changes(self):
        rows = self.legacy_story()
        for index, column, value in ((-2, "name", "adv_other.txt"), (-1, "id", "unknown")):
            with self.subTest(index=index):
                changed = [dict(row) for row in rows]
                changed[index][column] = value
                self.write_story_rows(changed)
                before = self.csv_path.read_bytes()
                with self.assertRaisesRegex(ValueError, "source metadata or row count"):
                    self.pipeline.refresh()
                self.assertEqual(self.csv_path.read_bytes(), before)

    def test_same_hash_migration_checks_all_files_before_publishing(self):
        self.legacy_story()
        early_source = self.pipeline.source_dir / "adv_a.txt"
        early_source.write_bytes((self.pipeline.source_dir / "adv_test.txt").read_bytes())
        early_csv = self.pipeline.csv_dir / "adv_a.csv"
        early_csv.write_text(self.csv_path.read_text().replace("adv_test.txt", "adv_a.txt"))
        earlier_bytes = early_csv.read_bytes()
        self.csv_path.write_text(self.csv_path.read_text().replace("1:text:1", "999:text:1"))
        with self.assertRaisesRegex(ValueError, "CSV source mismatch"):
            self.pipeline.refresh()
        self.assertEqual(early_csv.read_bytes(), earlier_bytes)
        self.assertFalse((self.root / ".analysis/rebase-backup").exists())

    def test_refresh_preserves_unchanged_text_after_line_shift(self):
        self.pipeline.run()
        source = self.pipeline.source_dir / "adv_test.txt"
        source.write_text("[timeline]\n" + source.read_text(encoding="utf-8"),
                          encoding="utf-8")
        result = self.pipeline.refresh()
        self.assertEqual(result["unmatched"], 0)
        self.assertGreaterEqual(result["preserved"], 1)
        self.assertEqual(self.pipeline.run()["rebuilt"], 1)
        self.assertIn("text=你好", (self.pipeline.txt_dir / "adv_test.txt").read_text())

    def test_refresh_reports_changed_source_before_dropping_translation(self):
        self.pipeline.run()
        source = self.pipeline.source_dir / "adv_test.txt"
        source.write_text(source.read_text(encoding="utf-8").replace(
            "こんにちは", "さようなら"), encoding="utf-8")
        old_csv = self.csv_path.read_bytes()
        with self.assertRaisesRegex(ValueError, "translated fields no longer match"):
            self.pipeline.refresh()
        self.assertEqual(self.csv_path.read_bytes(), old_csv)
        report = json.loads((self.root / ".analysis/rebase-report.json").read_text())
        self.assertEqual(len(report["unmatched"]), 1)
        self.assertEqual(self.pipeline.refresh(accept_dropped=True)["unmatched"], 1)
        self.assertTrue(list((self.root / ".analysis/rebase-backup").glob("*.csv")))
        self.assertEqual(self.pipeline.run()["rebuilt"], 1)


if __name__ == "__main__":
    unittest.main()
