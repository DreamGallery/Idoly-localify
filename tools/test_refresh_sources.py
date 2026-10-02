import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import refresh_sources as refresh


class RefreshSourceTests(unittest.TestCase):
    def test_notice_retry_only_after_resumable_partial_translation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'translate-notice.log').write_text('Notice: 672 new strings, 672 JSON values updated, 5 pending')
            calls = []
            def command(stage, argv, **kwargs):
                calls.append((stage, argv))
                return 0 if len(calls) == 3 else 1
            refresh.translate_notices(command, ['normal'], ['protected', '--log-file', 'same-log'], root)
            self.assertEqual([c[0] for c in calls], ['translate-notice', 'translate-notice-retry-1', 'translate-notice-retry-2'])
            self.assertEqual(calls[1][1][-2:], ['--batch-size', '4'])
            self.assertEqual(calls[2][1][-2:], ['--batch-size', '1'])

    def test_notice_retry_never_hides_initialization_or_api_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for text in ('bad configuration', 'Translation API returned HTTP 401\nNotice: 0 new strings, 0 JSON values updated, 5 pending'):
                (root / 'translate-notice.log').write_text(text)
                with patch('builtins.print'), patch.object(refresh, 'read_json'):
                    calls = []
                    def command(*args, **kwargs):
                        calls.append(args)
                        return 1
                    with self.assertRaises(RuntimeError):
                        refresh.translate_notices(command, ['normal'], ['protected'], root)
                    self.assertEqual(len(calls), 1)

    def test_notice_retry_success_skips_fallback_and_exhaustion_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            calls = []
            def success(*args, **kwargs):
                calls.append(args)
                return 0
            refresh.translate_notices(success, ['normal'], ['protected'], root)
            self.assertEqual(len(calls), 1)
            (root / 'translate-notice.log').write_text('Notice: 0 new strings, 0 JSON values updated, 5 pending')
            with self.assertRaisesRegex(RuntimeError, 'still incomplete'):
                refresh.translate_notices(lambda *a, **k: 1, ['normal'], ['protected'], root)

    def test_reuse_receipt_binds_sources_but_not_translations(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = SimpleNamespace(work_dir=root / "work", source_dir=root / "scripts",
                                   toolkit_dir=root / "toolkit", app_version="test")
            for name in ("work/master-tag.pb", "work/masterdata/Story.json", "scripts/adv_one.txt",
                         "toolkit/cache/OctoManifest.json", "work/notice-index.json", "work/notice-pages.json"):
                p = root / name; p.parent.mkdir(parents=True, exist_ok=True); p.write_text("{}");
            before = refresh.fetched_state(args)
            (root / "translation.json").write_text('"human revision"')
            self.assertEqual(before, refresh.fetched_state(args))
            (args.source_dir / "adv_one.txt").write_text("changed official source")
            self.assertNotEqual(before, refresh.fetched_state(args))

    def test_dry_run_never_runs_network_or_writes(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "not-created"
            with patch.object(refresh.subprocess, "run", side_effect=AssertionError("network")):
                self.assertEqual(refresh.main(["--solis-dir", temp, "--app-version", "test",
                    "--work-dir", str(output), "--translate", "--dry-run"]), 0)
            self.assertFalse(output.exists())

    def test_verified_scripts_only_and_idempotence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root / "source"; target = root / "target"
            source.mkdir(); data = b"official script"
            (source / "adv_one.txt").write_bytes(data)
            manifest = {"resourceList": [{"name": "adv_one.txt", "md5": hashlib.md5(data).hexdigest()}]}
            self.assertEqual(refresh.synchronize_scripts(source, target, manifest)["added"], 1)
            self.assertEqual(refresh.synchronize_scripts(source, target, manifest)["unchanged"], 1)
            (source / "adv_one.txt").write_bytes(b"corrupt")
            with self.assertRaises(ValueError): refresh.synchronize_scripts(source, target, manifest)
            self.assertEqual((target / "adv_one.txt").read_bytes(), data)

    def test_difference_distinguishes_new_changed_and_removed(self):
        self.assertEqual(refresh.field_difference({"a": "old", "b": "same", "c": "gone"},
                         {"a": "new", "b": "same", "d": "added"}),
                         {"added": 1, "changed": 1, "removed": 1})

    def test_story_memory_preserves_review_and_binds_source_and_speaker(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); path = root / "adv_one.csv"; path.touch()
            memory = root / "memory.json"
            rows = [{"id": "1:text:0", "name": "speaker", "text": "original", "trans": "reviewed"}, {}, {}]
            module = SimpleNamespace(load_csv=lambda _: rows, verify_source=lambda *_: None,
                        save_csv=lambda *_: None, validate_translation=lambda *_: None)
            refresh.story_memory(module, root, root, memory, restore=False)
            rows[0]["trans"] = "new human revision"
            self.assertEqual(refresh.story_memory(module, root, root, memory, restore=True), 0)
            self.assertEqual(rows[0]["trans"], "new human revision")
            rows[0]["trans"] = ""
            rows[0]["name"] = "different speaker"
            self.assertEqual(refresh.story_memory(module, root, root, memory, restore=True), 0)
            rows[0]["name"] = "speaker"
            rows[0]["text"] = "changed official source"
            self.assertEqual(refresh.story_memory(module, root, root, memory, restore=True), 0)
            rows[0]["text"] = "original"
            self.assertEqual(refresh.story_memory(module, root, root, memory, restore=True), 1)
            self.assertEqual(rows[0]["trans"], "new human revision")


if __name__ == "__main__":
    unittest.main()
