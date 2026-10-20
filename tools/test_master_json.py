import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import master_json
from master_identity import record_key
from master_json import (build, build_keyed, export, plan, read_table,
                         text_fields, unclassified_fields)


class MasterJsonTests(unittest.TestCase):
    def test_business_identity_survives_reorder_insert_and_source_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw, orig, zh = (root / name for name in ("raw", "orig", "zh"))
            raw.mkdir()
            a = {"key": "context.a", "word": "同じ原文"}
            b = {"key": "context.b", "word": "同じ原文"}
            ka, kb = (record_key("Wording", row) for row in (a, b))
            source_file = raw / "Wording.json"
            source_file.write_text(json.dumps([a, b]))
            export(raw, orig, zh)
            (zh / "Wording.json").write_text(json.dumps({
                ka: {"word": "不同语境甲"}, kb: {"word": "同じ原文"}}))
            c = {"key": "context.c", "word": "新しい文章"}
            source_file.write_text(json.dumps([b, c, a]))
            export(raw, orig, zh)
            self.assertEqual(read_table(zh / "Wording.json"), {
                ka: {"word": "不同语境甲"}, kb: {"word": "同じ原文"}})
            source_file.write_text(json.dumps([dict(a, word="変わった文章"), c]))
            export(raw, orig, zh)
            self.assertEqual(read_table(zh / "Wording.json"), {})
            self.assertNotIn(kb, read_table(orig / "Wording.json"))

    def test_unknown_missing_or_duplicate_identity_fails_before_writing(self):
        cases = [("Wording", [{"key": "a", "word": "文章"},
                              {"key": "a", "word": "別の文章"}]),
                 ("Wording", [{"word": "文章"}]),
                 ("NewTable", [{"word": "文章"}])]
        for table, rows in cases:
            with self.subTest(table=table, rows=rows), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                raw = root / "raw"
                raw.mkdir()
                (raw / f"{table}.json").write_text(json.dumps(rows))
                with self.assertRaises(ValueError):
                    export(raw, root / "orig", root / "zh")
                self.assertFalse((root / "orig").exists())
                self.assertFalse((root / "zh").exists())

    def test_legacy_business_keys_require_verified_migration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw, orig, zh = (root / name for name in ("raw", "orig", "zh"))
            for path in (raw, orig, zh):
                path.mkdir()
            (raw / "Wording.json").write_text(json.dumps([{"key": "real.key", "word": "文章"}]))
            (orig / "Wording.json").write_text('{"0":{"word":"文章"}}')
            (zh / "Wording.json").write_text('{"0":{"word":"译文"}}')
            before = [(path / "Wording.json").read_bytes() for path in (orig, zh)]
            with self.assertRaisesRegex(ValueError, "migrate identities"):
                export(raw, orig, zh)
            with self.assertRaisesRegex(ValueError, "migrate identities"):
                list(master_json.iter_fields(orig, zh))
            self.assertEqual(before, [(path / "Wording.json").read_bytes() for path in (orig, zh)])

    def test_plan_require_complete_fails_for_untranslated_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Story.json").write_text(
                json.dumps({"1": {"name": "日本語"}}, ensure_ascii=False), encoding="utf-8")
            (zh / "Story.json").write_text("{}", encoding="utf-8")
            command = [sys.executable, str(Path(master_json.__file__)), "plan",
                       "--orig-dir", str(orig), "--zh-dir", str(zh), "--require-complete"]
            incomplete = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(incomplete.returncode, 0)
            self.assertIn("0/1 MasterDB fields have values", incomplete.stdout)
            (zh / "Story.json").write_text(
                json.dumps({"1": {"name": "日语"}}, ensure_ascii=False), encoding="utf-8")
            complete = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(complete.returncode, 0, complete.stderr)
            self.assertIn("1/1 MasterDB fields have values", complete.stdout)

    def test_keyed_index_keeps_contextual_translation_and_retained_credit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            source = {"1": {"singer": "星見プロダクション",
                            "composer": "星見プロダクション"},
                      "2": {"name": "未翻訳"}}
            target = {"1": {"singer": "星见Production",
                            "composer": "星見プロダクション"},
                      "2": {"name": ""}}
            (orig / "Music.json").write_text(json.dumps(source, ensure_ascii=False),
                                             encoding="utf-8")
            translated = zh / "Music.json"
            translated.write_text(json.dumps(target, ensure_ascii=False), encoding="utf-8")
            output = root / "keyed.json"
            counts = build_keyed(orig, zh, output)
            self.assertEqual(counts, {"total_fields": 3, "indexed_fields": 2,
                                      "retained_fields": 1, "contextual_sources": 1,
                                      "contextual_fields": 2})
            data = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(data["tables"]["Music"]["1"], {
                "singer": ["星見プロダクション", "星见Production"],
                "composer": ["星見プロダクション", "星見プロダクション"]})
            self.assertNotIn("2", data["tables"]["Music"])
            self.assertEqual(build_keyed(orig, zh, output, verify=True), counts)
            target["1"]["singer"] = "星见事务所"
            translated.write_text(json.dumps(target, ensure_ascii=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "stale"):
                build_keyed(orig, zh, output, verify=True)

    def test_coverage_audit_allows_known_character_metadata(self):
        record = {"firstName": "麻奈", "groupName": "月のテンペスト",
                  "cv": "神田沙也加", "threeSize": "ー",
                  "loveInfos": [{"cv": "声優"}],
                  "description": "伝説のアイドル"}
        self.assertEqual(unclassified_fields(record, "Character",
                                             list(text_fields(record))), [])

    def test_export_rejects_new_unclassified_japanese_field(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw, orig, zh = (root / name for name in ("raw", "orig", "zh"))
            raw.mkdir()
            (raw / "Story.json").write_text(json.dumps([
                {"id": "story-1", "name": "第１話", "bannerCaption": "新しい物語"}
            ], ensure_ascii=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "bannerCaption"):
                export(raw, orig, zh)
            self.assertFalse((orig / "Story.json").exists())
            self.assertFalse((zh / "Story.json").exists())

    def test_text_list_exports_indexed_tutorial_messages(self):
        record = {"texts": ["先輩！", "Tap", "今日も頑張ろう"],
                  "settings": {"texts": ["次へ", "Skip"]}}
        self.assertEqual(list(text_fields(record)), [
            ("texts[0]", "先輩！"), ("texts[2]", "今日も頑張ろう"),
            ("settings.texts[0]", "次へ")])

    def test_music_credits_are_text(self):
        self.assertEqual(list(text_fields({"singer": "月のテンペスト",
                                           "lyricist": "たなかまゆ",
                                           "composer": "光増ハジメ",
                                           "arranger": "岡田ピロー"})),
                         [("singer", "月のテンペスト"),
                          ("lyricist", "たなかまゆ"),
                          ("composer", "光増ハジメ"),
                         ("arranger", "岡田ピロー")])

    def test_retained_record_blocks_ambiguous_global_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / 'orig', root / 'zh'
            orig.mkdir(); zh.mkdir()
            (orig / 'Music.json').write_text(json.dumps({'1': {'singer': '星見', 'composer': '星見'}}))
            (zh / 'Music.json').write_text(json.dumps({'1': {'singer': '星见', 'composer': '星見'}}))
            output = root / 'compiled.json'
            self.assertEqual(build(orig, zh, output), (0, 1))
            result = json.loads(output.read_text())
            self.assertNotIn('星見', result['text'])
            self.assertEqual(set(result['conflicts']['星見']), {'星見', '星见'})

    def test_music_credits_keep_original_spelling_on_refresh(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            source.mkdir()
            zh.mkdir()
            (source / "Music.json").write_text(json.dumps([
                {"id": "1", "name": "日本語曲", "lyricist": "岡田ピロー"}
            ], ensure_ascii=False), encoding="utf-8")
            (zh / "Music.json").write_text(json.dumps({
                "1": {"lyricist": "冈田ピロー"}
            }, ensure_ascii=False), encoding="utf-8")
            self.assertEqual(export(source, orig, zh), (1, 2))
            self.assertEqual(read_table(zh / "Music.json")["1"]["lyricist"],
                             "岡田ピロー")
            self.assertEqual(plan(orig, zh), (2, 1, 1))
            self.assertEqual(build(orig, zh, root / "text.json"), (0, 0))

    def test_build_adds_flattened_character_short_profile_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Character.json").write_text(json.dumps({
                "char-a": {"shortProfile": "伝説の\nアイドル"}
            }, ensure_ascii=False), encoding="utf-8")
            (zh / "Character.json").write_text(json.dumps({
                "char-a": {"shortProfile": "传说中的\n偶像"}
            }, ensure_ascii=False), encoding="utf-8")
            output = root / "text.json"
            self.assertEqual(build(orig, zh, output), (1, 0))
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["aliases"],
                             {"伝説のアイドル": "传说中的偶像"})

    def test_story_title_can_reflow_but_dialogue_line_breaks_cannot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Story.json").write_text(json.dumps({
                "story-1": {"name": "世界に\n生まれた\n声",
                            "description": "元の\n説明"}
            }, ensure_ascii=False), encoding="utf-8")
            target = {"story-1": {"name": "诞生之声", "description": "原来的\n说明"}}
            (zh / "Story.json").write_text(json.dumps(target, ensure_ascii=False),
                                           encoding="utf-8")
            self.assertEqual(plan(orig, zh), (2, 2, 0))
            target["story-1"]["description"] = "原来的说明"
            (zh / "Story.json").write_text(json.dumps(target, ensure_ascii=False),
                                           encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "line ending count changed"):
                plan(orig, zh)

    def test_builds_composed_story_player_header(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Story.json").write_text(json.dumps({
                "st-original-cmn-01-01-28": {"name": "この世界に\n生まれた瞬間\nの声"},
                "st-original-cmn-01-01-28-limited": {"name": "この世界に\n生まれた瞬間\nの声"}
            }, ensure_ascii=False), encoding="utf-8")
            (zh / "Story.json").write_text(json.dumps({
                "st-original-cmn-01-01-28": {"name": "诞生瞬间之声"},
                "st-original-cmn-01-01-28-limited": {"name": "诞生瞬间之声"}
            }, ensure_ascii=False), encoding="utf-8")
            output = root / "master.json"
            self.assertEqual(build(orig, zh, output), (1, 0))
            self.assertEqual(json.loads(output.read_text(encoding="utf-8"))["aliases"],
                             {"第28话 この世界に生まれた瞬間の声":
                              "第28话 诞生瞬间之声"})

    def test_export_preserves_matching_translation_and_excludes_people(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            source.mkdir()
            (source / "Character.json").write_text(json.dumps([
                {"id": "1", "name": "長瀬麻奈", "description": "伝説のアイドル",
                 "loveInfo": {"name": "麻奈", "description": "特別な話"}}
            ]), encoding="utf-8")
            (source / "Story.json").write_text(json.dumps([
                {"id": "9", "name": "長瀬麻奈", "description": "春の始まり",
                 "branchFirstText": "新しい物語"}
            ]), encoding="utf-8")
            names = root / "names.json"
            names.write_text(json.dumps({"長瀬麻奈": "长濑麻奈"}), encoding="utf-8")
            seeds = root / "dict.json"
            seeds.write_text(json.dumps({"text": {"春の始まり": "春天的开始"}}),
                             encoding="utf-8")
            self.assertEqual(export(source, orig, zh, seeds, names), (2, 4))
            self.assertEqual(read_table(orig / "Character.json"),
                             {"1": {"description": "伝説のアイドル",
                                    "loveInfo.description": "特別な話"}})
            self.assertEqual(read_table(zh / "Story.json"),
                             {"9": {"description": "春天的开始"}})
            self.assertEqual(plan(orig, zh), (4, 1, 3))
            self.assertEqual(build(orig, zh, root / "text.json"), (1, 0))
            # Keep a reviewer's translation on refresh while the source matches.
            data = read_table(zh / "Character.json")
            data["1"] = {"description": "传说中的偶像"}
            (zh / "Character.json").write_text(json.dumps(data), encoding="utf-8")
            export(source, orig, zh, seeds, names)
            self.assertEqual(read_table(zh / "Character.json")["1"]["description"],
                             "传说中的偶像")
            # Invalidate translation when the underlying original changes.
            (source / "Character.json").write_text(json.dumps([
                {"id": "1", "name": "長瀬麻奈", "description": "新たな伝説",
                 "loveInfo": {"name": "麻奈", "description": "特別な話"}}
            ]), encoding="utf-8")
            export(source, orig, zh, seeds, names)
            self.assertNotIn("description", read_table(zh / "Character.json").get("1", {}))

    def test_export_preflights_all_tables_before_replacing_any_json(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            old_orig = {"1": {"text": "以前の文章"}}
            old_zh = {"1": {"text": "以前的句子"}}
            (orig / "A.json").write_text(json.dumps(old_orig, ensure_ascii=False),
                                         encoding="utf-8")
            (zh / "A.json").write_text(json.dumps(old_zh, ensure_ascii=False),
                                       encoding="utf-8")
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            (source / "B.json").write_text(json.dumps(["invalid record"]), encoding="utf-8")
            before = ((orig / "A.json").read_bytes(), (zh / "A.json").read_bytes())
            with self.assertRaisesRegex(ValueError, "record 0 is not an object"):
                export(source, orig, zh)
            self.assertEqual(((orig / "A.json").read_bytes(),
                              (zh / "A.json").read_bytes()), before)
            self.assertFalse((root / ".master-archive").exists())

    def test_interrupted_export_uses_archived_sources_on_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            old_orig = {"1": {"text": "以前の文章"}}
            old_zh = {"1": {"text": "以前的句子"}}
            (orig / "A.json").write_text(json.dumps(old_orig, ensure_ascii=False),
                                         encoding="utf-8")
            (zh / "A.json").write_text(json.dumps(old_zh, ensure_ascii=False),
                                       encoding="utf-8")
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            real_copy = master_json._atomic_copy
            copies = 0

            def interrupt_after_first_copy(staged, target):
                nonlocal copies
                copies += 1
                if copies == 2:
                    raise OSError("simulated interruption")
                real_copy(staged, target)

            with patch.object(master_json, "_atomic_copy", interrupt_after_first_copy):
                with self.assertRaisesRegex(OSError, "simulated interruption"):
                    export(source, orig, zh)
            marker = root / ".master-export-in-progress.json"
            self.assertTrue(marker.is_file())
            self.assertEqual(read_table(orig / "A.json"),
                             {"1": {"text": "新しい文章"}})
            self.assertEqual(read_table(zh / "A.json"), old_zh)
            with self.assertRaisesRegex(ValueError, "interrupted"):
                plan(orig, zh)
            # A different raw dump must not reuse the old recovery snapshot.
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "さらに新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "different input or mode"):
                export(source, orig, zh)
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            (zh / "A.json").write_text(json.dumps({
                "1": {"text": "人工新译"}
            }, ensure_ascii=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "changed outside interrupted export"):
                export(source, orig, zh)
            (zh / "A.json").write_text(json.dumps(old_zh, ensure_ascii=False),
                                       encoding="utf-8")
            self.assertEqual(export(source, orig, zh), (1, 1))
            self.assertFalse(marker.exists())
            self.assertEqual(read_table(zh / "A.json"), {})
            self.assertEqual(plan(orig, zh), (1, 0, 1))
            snapshots = list((root / ".master-archive").iterdir())
            self.assertEqual(len(snapshots), 1)
            self.assertEqual(read_table(snapshots[0] / "orig/A.json"), old_orig)
            self.assertEqual(read_table(snapshots[0] / "zh-Hans/A.json"), old_zh)

    def test_export_rejects_translation_edit_during_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            old = {"1": {"text": "以前の文章"}}
            for directory_path in (orig, zh):
                (directory_path / "A.json").write_text(json.dumps(old, ensure_ascii=False),
                                                         encoding="utf-8")
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            real_fields = master_json.text_fields
            changed = False

            def edit_while_staging(record, path=""):
                nonlocal changed
                if not changed:
                    changed = True
                    (zh / "A.json").write_text(json.dumps({
                        "1": {"text": "人工修改"}
                    }, ensure_ascii=False), encoding="utf-8")
                yield from real_fields(record, path)

            with patch.object(master_json, "text_fields", edit_while_staging):
                with self.assertRaisesRegex(ValueError, "changed during export"):
                    export(source, orig, zh)
            self.assertEqual(read_table(orig / "A.json"), old)
            self.assertEqual(read_table(zh / "A.json"), {"1": {"text": "人工修改"}})
            self.assertFalse((root / ".master-export-in-progress.json").exists())

    def test_partial_export_preserves_unmentioned_tables(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            old = {"2": {"text": "別の文章"}}
            for path in (orig / "B.json", zh / "B.json"):
                path.write_text(json.dumps(old, ensure_ascii=False), encoding="utf-8")
            self.assertEqual(export(source, orig, zh), (1, 1))
            self.assertTrue((orig / "B.json").is_file())
            self.assertTrue((zh / "B.json").is_file())

    def test_full_export_archives_and_prunes_retired_table(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            old = {"2": {"text": "別の文章"}}
            for path in (orig / "B.json", zh / "B.json"):
                path.write_text(json.dumps(old, ensure_ascii=False), encoding="utf-8")
            archive = root / "archive"
            self.assertEqual(export(source, orig, zh, prune_missing=True,
                                    archive_dir=archive), (1, 1))
            self.assertFalse((orig / "B.json").exists())
            self.assertFalse((zh / "B.json").exists())
            snapshots = list(archive.iterdir())
            self.assertEqual(len(snapshots), 1)
            self.assertEqual(read_table(snapshots[0] / "orig/B.json"), old)
            self.assertEqual(read_table(snapshots[0] / "zh-Hans/B.json"), old)
            manifest = json.loads((snapshots[0] / "manifest.json").read_text())
            self.assertEqual(manifest["retired"], ["B.json"])

    def test_unchanged_export_does_not_create_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            source.mkdir()
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "同じ文章"}
            ], ensure_ascii=False), encoding="utf-8")
            self.assertEqual(export(source, orig, zh), (1, 1))
            self.assertEqual(export(source, orig, zh), (1, 1))
            self.assertFalse((root / ".master-archive").exists())

    def test_prune_rejects_an_obviously_partial_raw_dump(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            (source / "A.json").write_text(json.dumps([
                {"id": "1", "text": "新しい文章"}
            ], ensure_ascii=False), encoding="utf-8")
            old = {"2": {"text": "別の文章"}}
            for name in ("B.json", "C.json"):
                for directory_path in (orig, zh):
                    (directory_path / name).write_text(json.dumps(old, ensure_ascii=False),
                                                        encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "may be partial"):
                export(source, orig, zh, prune_missing=True)
            self.assertTrue((orig / "B.json").is_file())
            self.assertTrue((zh / "C.json").is_file())

    def test_invalid_raw_table_type_does_not_retire_existing_table(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, orig, zh = (root / name for name in ("master", "orig", "zh"))
            for path in (source, orig, zh):
                path.mkdir()
            (source / "A.json").write_text("{}", encoding="utf-8")
            old = {"1": {"text": "以前の文章"}}
            for directory_path in (orig, zh):
                (directory_path / "A.json").write_text(json.dumps(old, ensure_ascii=False),
                                                         encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "expected a list of records"):
                export(source, orig, zh, prune_missing=True)
            self.assertEqual(read_table(orig / "A.json"), old)


if __name__ == "__main__":
    unittest.main()
