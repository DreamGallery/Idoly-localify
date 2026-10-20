import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from export_translation_sources import snapshot
from master_identity import BUSINESS_KEY_RULES, record_key, rules_metadata
from master_field_paths import FieldPaths, field_rules_metadata
import migrate_master_keys as migration


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def read(path):
    return json.loads(path.read_text(encoding="utf-8"))


class MigrationTests(unittest.TestCase):
    def setUp(self):
        local = Path(__file__).resolve().parents[1] / ".agent-local"
        local.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="master-key-tests-", dir=local)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repository, self.raw = self.root / "repository", self.root / "raw"
        for folder in (self.repository / "master/orig", self.repository / "master/zh-Hans",
                       self.repository / ".agent-local", self.raw):
            folder.mkdir(parents=True)
        (self.repository / ".agent-local/.gitignore").write_text("*\n", encoding="utf-8")
        self.version = "verified-test-version"
        self.rows = [
            {"key": "word-a", "word": "同じ原文", "description": "説明"},
            {"key": "word-b", "word": "同じ原文", "description": "別の説明"},
            {"key": "omitted", "word": "対象外"},
        ]
        self.add_table("Wording", self.rows,
                       {"0": {"word": "同じ原文", "description": "説明"},
                        "1": {"word": "同じ原文"}},
                       {"0": {"word": "甲译文", "description": ""},
                        "1": {"word": "乙译文"}})
        self.add_table("StaffTraining", [
            {"parameterType": 0, "name": "未指定"},
            {"parameterType": 1, "name": "歌唱"},
            {"parameterType": 2, "name": "演技"},
        ], {"0": {"name": "未指定"}, "2": {"name": "演技"}},
            {"0": {"name": "未指定"}})
        # Standard IDs retain their original formatting and bytes.
        (self.repository / "master/orig/Card.json").write_bytes(b'{"0001":{"name":"\\u30ab\\u30fc\\u30c9"}}')
        (self.repository / "master/zh-Hans/Card.json").write_bytes(b'{"0001":{"name":"card"}}\n\n')
        self.refresh_state()
        self.refresh_upstream()

    def add_table(self, name, rows, orig, zh):
        write(self.raw / (name + ".json"), rows)
        write(self.repository / f"master/orig/{name}.json", orig)
        write(self.repository / f"master/zh-Hans/{name}.json", zh)

    def refresh_state(self):
        write(self.raw / migration.FETCH_STATE, {
            "version": self.version, "complete_version": self.version,
            "tables": {path.stem: {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                       for path in self.raw.glob("*.json") if path.name != migration.FETCH_STATE},
        })

    def refresh_upstream(self):
        path = self.repository / "upstream.json"
        upstream = read(path) if path.exists() else {
            "schema_version": 1, "story": {"resource_revision": "preserved"},
            "master": {"master_tag_version": self.version},
        }
        upstream["master"]["orig_snapshot_sha256"] = snapshot(
            list((self.repository / "master/orig").glob("*.json")))
        write(path, upstream)

    def live(self):
        return {str(path.relative_to(self.repository)): path.read_bytes()
                for path in self.repository.rglob("*")
                if path.is_file() and ".agent-local" not in path.relative_to(self.repository).parts}

    def assert_rejected_without_writes(self, pattern):
        before = self.live()
        local_before = sorted((self.repository / ".agent-local").rglob("*"))
        with self.assertRaisesRegex(ValueError, pattern):
            migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(self.live(), before)
        self.assertEqual(sorted((self.repository / ".agent-local").rglob("*")), local_before)

    def test_default_dry_run_changes_no_files(self):
        before = self.live()
        report = migration.run(self.raw, self.repository)
        self.assertEqual(report["status"], "dry-run")
        self.assertEqual(report["legacy_records"], 4)
        self.assertIsNone(report["journal"])
        self.assertEqual(self.live(), before)
        self.assertEqual(list((self.repository / ".agent-local").iterdir()),
                         [self.repository / ".agent-local/.gitignore"])

    def test_apply_preserves_values_subsets_zero_and_standard_tables(self):
        before = self.live()
        old_orig = read(self.repository / "master/orig/Wording.json")
        old_zh = read(self.repository / "master/zh-Hans/Wording.json")
        upstream = read(self.repository / "upstream.json")
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(result["status"], "applied")
        new_orig = read(self.repository / "master/orig/Wording.json")
        new_zh = read(self.repository / "master/zh-Hans/Wording.json")
        self.assertEqual(new_orig, {record_key("Wording", self.rows[int(key)]): value
                                   for key, value in old_orig.items()})
        self.assertEqual(new_zh, {record_key("Wording", self.rows[int(key)]): value
                                 for key, value in old_zh.items()})
        self.assertEqual(new_zh[record_key("Wording", self.rows[0])]["word"], "甲译文")
        self.assertEqual(new_zh[record_key("Wording", self.rows[1])]["word"], "乙译文")
        self.assertEqual(new_zh[record_key("Wording", self.rows[0])]["description"], "")
        self.assertNotIn(record_key("Wording", self.rows[2]), new_orig)
        staff = read(self.repository / "master/zh-Hans/StaffTraining.json")
        self.assertEqual(staff, {record_key("StaffTraining", {"parameterType": 0}): {"name": "未指定"}})
        self.assertEqual(read(self.repository / migration.METADATA), rules_metadata())
        for path in ("master/orig/Card.json", "master/zh-Hans/Card.json"):
            self.assertEqual((self.repository / path).read_bytes(), before[path])
        after_upstream = read(self.repository / "upstream.json")
        self.assertEqual(after_upstream["master"]["master_tag_version"], self.version)
        upstream["master"]["orig_snapshot_sha256"] = snapshot(
            list((self.repository / "master/orig").glob("*.json")))
        self.assertEqual(after_upstream, upstream)
        journal = Path(result["journal"])
        self.assertTrue(journal.is_file())
        self.assertEqual((journal.parent / "before/master/orig/Wording.json").read_bytes(),
                         before["master/orig/Wording.json"])
        self.assertFalse((self.repository / migration.MARKER).exists())
        self.assertEqual(read(journal)["mappings"]["Wording"]["0"], record_key("Wording", self.rows[0]))

    def test_rerun_is_idempotent(self):
        migration.migrate(self.raw, self.repository, apply=True)
        before, directories = self.live(), list((self.repository / migration.LOCAL_ROOT).iterdir())
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(result["status"], "unchanged")
        self.assertEqual(result["changed"], [])
        self.assertEqual(result["legacy_records"], 0)
        self.assertEqual(self.live(), before)
        self.assertEqual(list((self.repository / migration.LOCAL_ROOT).iterdir()), directories)

    def test_nested_paths_preserve_same_text_different_translations_and_empty_values(self):
        row = {"id": "001", "details": [
            {"messageDetailId": "b", "text": "同じ原文", "choiceText": "選択"},
            {"messageDetailId": "a", "text": "同じ原文"},
        ]}
        orig = {"001": {"details[0].text": "同じ原文",
                        "details[0].choiceText": "選択", "details[1].text": "同じ原文"}}
        zh = {"001": {"details[0].text": "乙译文",
                      "details[0].choiceText": "", "details[1].text": "同じ原文"}}
        self.add_table("Message", [row], orig, zh)
        self.refresh_state()
        self.refresh_upstream()
        result = migration.migrate(self.raw, self.repository, apply=True)
        paths = FieldPaths("Message", row)
        for language, old in (("orig", orig), ("zh-Hans", zh)):
            self.assertEqual(read(self.repository / f"master/{language}/Message.json"),
                             {"001": {paths.canonicalize(field): value
                                      for field, value in old["001"].items()}})
        self.assertEqual(result["positional_fields"], 3)
        self.assertEqual(read(self.repository / migration.FIELD_METADATA), field_rules_metadata())
        journal = read(Path(result["journal"]))
        self.assertEqual(journal["field_mappings"]["Message"]["001"]["details[0].text"],
                         'details[key={"messageDetailId":"b"}].text')
        self.assertEqual(migration.migrate(self.raw, self.repository)["status"], "unchanged")

    def test_existing_record_key_and_unkeyed_inner_string_list(self):
        row = {"type": 0, "stepInfo": [
            {"step": 0, "subStep": 2, "texts": ["始め", "終わり"]}]}
        key = record_key("Tutorial", row)
        self.add_table("Tutorial", [row], {key: {"stepInfo[0].texts[1]": "終わり"}},
                       {key: {"stepInfo[0].texts[1]": "结束"}})
        self.refresh_state()
        self.refresh_upstream()
        self.interrupt_after_one_file()
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertTrue(result["resumed"])
        self.assertEqual(read(self.repository / "master/zh-Hans/Tutorial.json"), {
            key: {'stepInfo[key={"step":0,"subStep":2}].texts[1]': "结束"}})

    def test_duplicate_nested_identity_and_duplicate_path_alias_fail_without_writes(self):
        row = {"id": "parent", "details": [
            {"messageDetailId": "a", "text": "同じ原文"},
            {"messageDetailId": "a", "text": "別の原文"}]}
        self.add_table("Message", [row], {"parent": {"details[0].text": "同じ原文"}}, {})
        self.refresh_state()
        self.refresh_upstream()
        self.assert_rejected_without_writes("duplicate nested identity")
        row["details"].pop()
        self.add_table("Message", [row], {"parent": {
            "details[0].text": "同じ原文",
            'details[key={"messageDetailId":"a"}].text': "同じ原文"}}, {})
        self.refresh_state()
        self.refresh_upstream()
        self.assert_rejected_without_writes("duplicate aliases")

    def test_version_mismatch_writes_nothing(self):
        state = read(self.raw / migration.FETCH_STATE)
        state["version"] = "different-version"
        write(self.raw / migration.FETCH_STATE, state)
        self.assert_rejected_without_writes("MasterTag version")

    def test_raw_hash_mismatch_writes_nothing(self):
        write(self.raw / "Wording.json", self.rows[::-1])
        self.assert_rejected_without_writes("sha256")

    def test_partial_fetch_cannot_bind_retained_older_tables_to_new_version(self):
        state = read(self.raw / migration.FETCH_STATE)
        upstream = read(self.repository / "upstream.json")
        state["version"] = upstream["master"]["master_tag_version"] = "new-version"
        write(self.repository / "upstream.json", upstream)
        # fetch_packs can retain these old, correctly hashed tables during a
        # partial update. Neither a new version label nor equal text proves
        # that their legacy row numbers belong to the new snapshot.
        for complete in (None, self.version):
            with self.subTest(complete_version=complete):
                state["complete_version"] = complete
                write(self.raw / migration.FETCH_STATE, state)
                self.assert_rejected_without_writes("full verified MasterDB fetch")

    def test_original_mismatch_writes_nothing_even_with_matching_snapshot(self):
        path = self.repository / "master/orig/Wording.json"
        original = read(path)
        original["0"]["word"] = "変わった原文"
        write(path, original)
        self.refresh_upstream()
        self.assert_rejected_without_writes("original differs")

    def test_entire_orig_snapshot_includes_standard_tables(self):
        path = self.repository / "master/orig/Card.json"
        path.write_bytes(path.read_bytes() + b"\n")
        self.assert_rejected_without_writes("orig_snapshot_sha256")

    def test_duplicate_raw_identity_is_rejected_even_for_omitted_row(self):
        rows = self.rows + [{"key": "omitted", "word": "別の原文"}]
        write(self.raw / "Wording.json", rows)
        self.refresh_state()
        self.assert_rejected_without_writes("duplicate identity")

    def test_invalid_and_out_of_range_indices(self):
        path = self.repository / "master/orig/Wording.json"
        translated = self.repository / "master/zh-Hans/Wording.json"
        for key in ("00", "-1", "+1", " 1", "1.0", "3", "999999999999999999999999"):
            with self.subTest(key=key):
                write(path, {key: {"word": "同じ原文"}})
                write(translated, {})
                self.refresh_upstream()
                self.assert_rejected_without_writes("canonical|out of range")

    def test_orphan_record_and_field_are_rejected(self):
        path = self.repository / "master/zh-Hans/Wording.json"
        for value in ({"3": {"word": "孤立"}}, {"0": {"name": "孤立"}}):
            with self.subTest(value=value):
                write(path, value)
                self.assert_rejected_without_writes("orphan translation")

    def test_incomplete_fetch_and_export_are_rejected(self):
        for root, marker in ((self.raw, migration.FETCH_MARKER),
                             (self.repository, migration.EXPORT_MARKER)):
            with self.subTest(marker=marker):
                write(root / marker, {})
                self.assert_rejected_without_writes("operation is incomplete")
                (root / marker).unlink()

    def interrupt_after_one_file(self):
        original_install = migration._install
        calls = 0
        def interrupted(*args):
            nonlocal calls
            original_install(*args)
            calls += 1
            if calls == 1:
                raise OSError("simulated interruption")
        with patch.object(migration, "_install", interrupted):
            with self.assertRaisesRegex(OSError, "simulated interruption"):
                migration.migrate(self.raw, self.repository, apply=True)
        self.assertTrue((self.repository / migration.MARKER).is_file())
        return read(self.repository / migration.MARKER)

    def test_interruption_resumes_same_journal_and_dry_run_stays_read_only(self):
        marker = self.interrupt_after_one_file()
        before = self.live()
        dry_run = migration.migrate(self.raw, self.repository)
        self.assertTrue(dry_run["resumed"])
        self.assertEqual(self.live(), before)
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(Path(result["journal"]), self.repository / marker["journal"])
        self.assertTrue(result["resumed"])
        self.assertFalse((self.repository / migration.MARKER).exists())
        self.assertEqual(migration.migrate(self.raw, self.repository)["status"], "unchanged")

    def test_recovery_after_all_files_written_before_marker_removal(self):
        check = migration._check_live
        def interrupted(*args, **kwargs):
            result = check(*args, **kwargs)
            if kwargs.get("final"):
                raise OSError("interrupted after final replacement")
            return result
        with patch.object(migration, "_check_live", interrupted):
            with self.assertRaisesRegex(OSError, "final replacement"):
                migration.migrate(self.raw, self.repository, apply=True)
        before = self.live()
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertTrue(result["resumed"])
        before.pop(migration.MARKER)
        self.assertEqual(self.live(), before)

    def test_recovery_refuses_external_edit_without_more_writes(self):
        self.interrupt_after_one_file()
        target = self.repository / "master/zh-Hans/Wording.json"
        target.write_bytes(target.read_bytes() + b"\n")
        before = self.live()
        with self.assertRaisesRegex(ValueError, "external edit"):
            migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(self.live(), before)
        self.assertTrue((self.repository / migration.MARKER).exists())

    def test_recovery_refuses_unrelated_file_edits_and_new_files(self):
        self.interrupt_after_one_file()
        target = self.repository / "master/orig/Card.json"
        old = target.read_bytes()
        target.write_bytes(old + b"\n")
        with self.assertRaisesRegex(ValueError, "external edit"):
            migration.migrate(self.raw, self.repository, apply=True)
        target.write_bytes(old)
        write(self.repository / "master/zh-Hans/NewTable.json", {})
        with self.assertRaisesRegex(ValueError, "file set changed"):
            migration.migrate(self.raw, self.repository, apply=True)

    def test_recovery_revalidates_raw_and_staged_files(self):
        marker = self.interrupt_after_one_file()
        raw_path = self.raw / "Wording.json"
        old = raw_path.read_bytes()
        raw_path.write_bytes(old + b"\n")
        with self.assertRaisesRegex(ValueError, "sha256"):
            migration.migrate(self.raw, self.repository, apply=True)
        raw_path.write_bytes(old)
        staged = (self.repository / marker["journal"]).parent / "after/master/orig/Wording.json"
        staged.write_bytes(staged.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "staged file hash changed"):
            migration.migrate(self.raw, self.repository, apply=True)

    def test_recovery_cannot_write_outside_allowed_paths(self):
        marker = self.interrupt_after_one_file()
        marker["journal"] = "../outside/journal.json"
        write(self.repository / migration.MARKER, marker)
        with self.assertRaisesRegex(ValueError, "Invalid migration marker"):
            migration.migrate(self.raw, self.repository, apply=True)

    def test_symlinked_sources_or_backups_are_rejected(self):
        original = self.repository / "master/orig/Wording.json"
        elsewhere = self.root / "saved.json"
        original.rename(elsewhere)
        original.symlink_to(elsewhere)
        self.assert_rejected_without_writes("Symlinked")
        original.unlink()
        elsewhere.rename(original)
        (self.repository / migration.LOCAL_ROOT).symlink_to(self.raw, target_is_directory=True)
        self.assert_rejected_without_writes("Symlinked")

    def test_duplicate_json_keys_are_rejected(self):
        path = self.repository / "master/orig/Wording.json"
        path.write_text('{"0":{"word":"同じ原文"},"0":{"word":"同じ原文"}}', encoding="utf-8")
        self.refresh_upstream()
        self.assert_rejected_without_writes("duplicate JSON key")

    def test_apply_requires_git_ignored_backup_paths(self):
        # An inner repository prevents the outer checkout's ignore rules from
        # masking a missing exclusion in the translation repository itself.
        subprocess.run(["git", "init", "--quiet", str(self.repository)], check=True,
                       capture_output=True, text=True)
        (self.repository / ".agent-local/.gitignore").unlink()
        self.assert_rejected_without_writes("must be Git-ignored")

    def test_all_fifteen_rules_migrate_without_losing_fields(self):
        for table, rule in BUSINESS_KEY_RULES.items():
            row = {name: "identity" if kind == "string" else 0 for name, kind in rule}
            row.update(name="日本語")
            self.add_table(table, [row], {"0": {"name": "日本語"}}, {"0": {"name": "中文"}})
        self.refresh_state()
        self.refresh_upstream()
        result = migration.migrate(self.raw, self.repository, apply=True)
        self.assertEqual(result["tables"], 15)
        for table in BUSINESS_KEY_RULES:
            raw = read(self.raw / (table + ".json"))[0]
            self.assertEqual(read(self.repository / f"master/zh-Hans/{table}.json"),
                             {record_key(table, raw): {"name": "中文"}})

    def test_cli_defaults_to_dry_run(self):
        before = self.live()
        result = subprocess.run(
            [sys.executable, str(Path(migration.__file__)), "--raw-dir", str(self.raw),
             "--repository", str(self.repository)],
            capture_output=True, text=True, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["status"], "dry-run")
        self.assertEqual(self.live(), before)


if __name__ == "__main__":
    unittest.main()
