"""Font payload checks must precede text writes and follow installed APK changes."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import font_deployment as fonts


class FontDeploymentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dictionary = self.root / "dict.json"
        self.dictionary.write_text(json.dumps({"font": True, "font_mode": "replace-ui"}))
        self.translation = "中文 Latin 12!"
        self.enterContext(patch.object(fonts, "translated_fields", side_effect=lambda root:
            iter([("translation", self.translation)])))
        self.enterContext(patch.object(fonts, "source_fields", side_effect=lambda root:
            iter([("source", "原始")])) )
        self.points = list(range(32, 127)) + sorted({ord(c) for c in "中文原始"})
        self.remote = "/data/app/~~ab_c==/game.idoly-x+==/base.apk"
        self.digest = "a" * 64
        self.adb = Mock(side_effect=lambda *args: (
            "absent\n" if args[-1].startswith("if [ -f ") else
            f"package:{self.remote}\npackage:/data/app/game/split.apk\n"
            if args[2:] == ("shell", "pm", "path", fonts.PACKAGE)
            else f"{self.digest}  {self.remote}\n"))

    def payload(self, mode="add-font", points=None):
        return {"patch_mode": mode, "font_sha256": "c" * 64,
                "codepoints": self.points if points is None else points}

    def cache(self, mode, digest=None, points=None):
        directory = self.root / ".analysis" / fonts.CACHE_DIRECTORY
        directory.mkdir(parents=True, exist_ok=True)
        digest = digest or self.digest
        (directory / f"{digest}.json").write_text(json.dumps({
            "schema_version": fonts.CACHE_SCHEMA, "apk_sha256": digest,
            **self.payload(mode, points)}))

    def check(self):
        return fonts.assert_compatible(self.adb, "adb", "emulator-5560", self.root, self.dictionary)

    def test_matching_installed_digest_uses_cache_without_pulling_apk(self):
        self.cache("add-font")
        with patch.object(fonts.subprocess, "run") as run:
            self.assertEqual(self.check(), "add-font")
            run.assert_not_called()

    def test_old_payload_is_rejected_even_with_new_external_dictionary(self):
        self.cache("replace-fot")
        with self.assertRaisesRegex(ValueError, "install the matching APK"):
            self.check()

    def test_new_payload_rejects_old_dictionary(self):
        self.cache("add-font")
        self.dictionary.write_text(json.dumps({"font": True, "font_mode": "replace-all"}))
        with self.assertRaisesRegex(ValueError, "incompatible"):
            self.check()

    def test_changed_installed_apk_does_not_reuse_old_receipt(self):
        self.cache("replace-fot", "b" * 64)
        with patch.object(fonts.subprocess, "run") as run, \
             patch.object(fonts, "file_digest", return_value=self.digest), \
             patch.object(fonts, "inspect_payload", return_value=self.payload()) as inspect:
            self.assertEqual(self.check(), "add-font")
            run.assert_called_once()
            inspect.assert_called_once()
        self.assertTrue((self.root / f".analysis/{fonts.CACHE_DIRECTORY}/{self.digest}.json").exists())

    def test_hash_change_during_pull_is_rejected_without_cache(self):
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value="b" * 64), \
             patch.object(fonts, "inspect_payload") as inspect:
            with self.assertRaisesRegex(ValueError, "changed during"):
                self.check()
            inspect.assert_not_called()
        self.assertFalse((self.root / f".analysis/{fonts.CACHE_DIRECTORY}/{self.digest}.json").exists())

    def test_unsafe_pm_path_is_rejected_before_shell_hash(self):
        self.adb = Mock(return_value="package:/data/app/$(command)/base.apk\n")
        with self.assertRaisesRegex(ValueError, "Cannot identify"):
            self.check()
        self.assertEqual(self.adb.call_count, 1)

    def test_cjk_subset_missing_latin_is_rejected(self):
        self.cache("add-font", points=[point for point in self.points if point != ord("L")])
        with self.assertRaisesRegex(ValueError, "Replacement font misses"):
            self.check()

    def test_missing_chinese_glyph_is_rejected(self):
        self.cache("add-font", points=[point for point in self.points if point != ord("文")])
        with self.assertRaisesRegex(ValueError, "Replacement font misses"):
            self.check()

    def test_same_apk_rechecks_new_translations(self):
        self.cache("add-font")
        self.assertEqual(self.check(), "add-font")
        self.translation += "新"
        with self.assertRaisesRegex(ValueError, "Replacement font misses"):
            self.check()

    def test_same_apk_rechecks_actual_deployment_dictionary(self):
        self.cache("add-font")
        self.assertEqual(self.check(), "add-font")
        self.dictionary.write_text(json.dumps({"font": True,
            "font_mode": "replace-ui", "text": {"key": "新"}}, ensure_ascii=False))
        with self.assertRaisesRegex(ValueError, "Replacement font misses"):
            self.check()

    def test_schema_one_cache_is_ignored_and_font_is_reinspected(self):
        old = self.root / ".analysis/font-payload-cache"
        old.mkdir(parents=True)
        (old / f"{self.digest}.json").write_text(json.dumps({
            "schema_version": 1, "apk_sha256": self.digest, "patch_mode": "add-font"}))
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value=self.digest), \
             patch.object(fonts, "inspect_payload", return_value=self.payload()) as inspect:
            self.assertEqual(self.check(), "add-font")
            inspect.assert_called_once()

    def module_adb(self, digest="d" * 64):
        original = self.adb.side_effect
        self.adb.side_effect = lambda *args: (
            "present\n" if args[-1].startswith("if [ -f ") else
            f"{digest}  {fonts.BUNDLE_PATH}\n"
            if fonts.BUNDLE_PATH in args[-1] else original(*args))

    def test_original_game_uses_verified_module_font(self):
        self.cache("original")
        self.module_adb()
        with patch.object(fonts.subprocess, "run") as run, \
             patch.object(fonts, "file_digest", return_value="d" * 64), \
             patch.object(fonts, "inspect_bundle", return_value=self.payload("module-font")) as inspect:
            self.assertEqual(self.check(), "module-font")
            inspect.assert_called_once()
            run.assert_called_once()
            self.assertEqual(self.check(), "module-font")
            self.assertEqual(run.call_count, 1)

    def test_module_font_takes_priority_over_embedded_font(self):
        self.cache("add-font")
        self.module_adb()
        with patch.object(fonts, "installed_bundle_payload", return_value=self.payload("module-font")) as bundle:
            self.assertEqual(self.check(), "module-font")
            bundle.assert_called_once()

    def test_invalid_module_bundle_does_not_silently_validate_embedded_font(self):
        self.cache("add-font")
        self.module_adb()
        with patch.object(fonts, "installed_bundle_payload", side_effect=ValueError("invalid bundle")):
            with self.assertRaisesRegex(ValueError, "invalid bundle"):
                self.check()

    def test_original_game_without_bundle_fails(self):
        self.cache("original")
        with self.assertRaisesRegex(ValueError, "installed module font bundle"):
            self.check()

    def test_changed_bundle_does_not_reuse_cached_font(self):
        self.cache("original")
        self.module_adb()
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value="d" * 64), \
             patch.object(fonts, "inspect_bundle", return_value=self.payload("module-font")):
            self.assertEqual(self.check(), "module-font")
        self.module_adb("e" * 64)
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value="e" * 64), \
             patch.object(fonts, "inspect_bundle", side_effect=ValueError("invalid bundle")) as inspect:
            with self.assertRaisesRegex(ValueError, "invalid bundle"):
                self.check()
            inspect.assert_called_once()

    def test_bundle_change_during_copy_is_rejected(self):
        self.cache("original")
        self.module_adb()
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value="e" * 64), \
             patch.object(fonts, "inspect_bundle") as inspect:
            with self.assertRaisesRegex(ValueError, "changed during"):
                self.check()
            inspect.assert_not_called()

    def test_module_font_still_requires_glyph_coverage(self):
        self.cache("original")
        self.module_adb()
        with patch.object(fonts.subprocess, "run"), \
             patch.object(fonts, "file_digest", return_value="d" * 64), \
             patch.object(fonts, "inspect_bundle", return_value=self.payload("module-font", [32])):
            with self.assertRaisesRegex(ValueError, "Replacement font misses"):
                self.check()

    def test_disabled_fonts_need_no_device_inspection(self):
        self.dictionary.write_text(json.dumps({"font": False, "font_mode": "replace-ui"}))
        self.assertIsNone(self.check())
        self.adb.assert_not_called()


if __name__ == "__main__":
    unittest.main()
