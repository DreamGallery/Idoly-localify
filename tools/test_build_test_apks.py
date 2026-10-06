from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from build_test_apks import (preflight, prepare_font_apk, verify_module_font_policy,
                             verify_module_font_bundle, verify_embedded, MODULE_PACKAGE)
from font_policy import DEFAULT_PATCH_MODE, validate_font_pair, required_patch_mode
import json
from zipfile import ZipFile
from io import BytesIO


class ApkPreflightTests(unittest.TestCase):
    def setUp(self):
        self.base = Path("base.apk")
        self.split = Path("split_config.arm64_v8a.apk")
        self.module = Path("app-debug.apk")
        self.base_info = {"name": "game.qualiarts.idolypride",
                          "versionCode": "405", "versionName": "6.0.2"}
        self.split_info = {"name": "game.qualiarts.idolypride",
                           "versionCode": "405", "versionName": "",
                           "split": "config.arm64_v8a"}

    def check(self, *infos, certificates=None, allow_untested=False):
        if not infos:
            infos = (self.base_info, self.split_info)
        if len(infos) == 2:
            infos += ({"name": "io.github.dreamgallery.idoly.localify"},)
        with patch("build_test_apks.package_info", side_effect=infos), patch(
                "build_test_apks.signer_certificates", side_effect=(
                    certificates or ({"original"}, {"original"}))):
            return preflight(self.base, self.split, self.module, "aapt", "apksigner", {},
                             allow_untested_version=allow_untested)

    def test_matching_original_apks_pass(self):
        self.assertEqual(self.check(), "6.0.2 (versionCode 405)")

    def test_mixed_version_or_signer_fails_before_patching(self):
        changed = dict(self.split_info, versionCode="406")
        with self.assertRaisesRegex(ValueError, "versionCode values differ"):
            self.check(self.base_info, changed)
        with self.assertRaisesRegex(ValueError, "signing certificates differ"):
            self.check(self.base_info, self.split_info,
                       certificates=({"original"}, {"different"}))

    def test_new_game_version_requires_explicit_test_override(self):
        updated_base = dict(self.base_info, versionName="6.1.0", versionCode="406")
        updated_split = dict(self.split_info, versionCode="406")
        with self.assertRaisesRegex(ValueError, "has not been tested"):
            self.check(updated_base, updated_split)
        self.assertEqual(self.check(updated_base, updated_split, allow_untested=True),
                         "6.1.0 (versionCode 406)")

    def test_wrong_split_or_module_is_rejected(self):
        wrong_split = dict(self.split_info, split="config.x86_64")
        with self.assertRaisesRegex(ValueError, "config.arm64_v8a"):
            self.check(self.base_info, wrong_split)
        with self.assertRaisesRegex(ValueError, "Module APK must be"):
            self.check(self.base_info, self.split_info, {"name": "other.module"})


class FontCacheTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.base, self.font = self.root / "original.apk", self.root / "font.otf"
        self.base.write_bytes(b"original game")
        self.font.write_bytes(b"requested font")
        self.cache, self.destination = self.root / "cache", self.root / "output.apk"
        def fake_patch(base, font, destination, **kwargs):
            destination.write_bytes(base.read_bytes() + b"/" + font.read_bytes())
        self.patch_apk = self.enterContext(patch.dict(sys.modules, {"patch_font": SimpleNamespace(
            patch_apk=fake_patch, font_bytes=lambda path, face: path.read_bytes(),
            embedded_font_bytes=lambda path, face, mode: path.read_bytes())}))
        self.patcher = self.enterContext(patch.object(sys.modules["patch_font"], "patch_apk", wraps=fake_patch))
        self.verify = self.enterContext(patch("build_test_apks.verify_font"))
        self.info = {"name": "game.qualiarts.idolypride", "versionCode": "405", "versionName": "6.0.2"}
        self.metadata = self.enterContext(patch("build_test_apks.package_info", return_value=self.info))
        self.version = self.enterContext(patch("build_test_apks.dependency_version", return_value="1.0"))

    def prepare(self):
        return prepare_font_apk(self.base, self.font, self.destination, self.cache, "aapt", {})

    def test_hit_skips_patching_but_rechecks_metadata_and_font_bytes(self):
        self.assertFalse(self.prepare())
        self.assertTrue(self.prepare())
        self.assertEqual(self.patcher.call_count, 1)
        self.assertEqual(self.verify.call_count, 2)
        self.assertEqual(self.metadata.call_count, 4)
        self.assertEqual(self.destination.read_bytes(), b"original game/requested font")

    def test_base_font_and_dependency_changes_create_new_entries(self):
        self.prepare()
        self.base.write_bytes(b"updated game")
        self.assertFalse(self.prepare())
        self.font.write_bytes(b"updated font")
        self.assertFalse(self.prepare())
        self.version.return_value = "2.0"
        self.assertFalse(self.prepare())
        self.assertEqual(len(list(self.cache.glob("*/manifest.json"))), 4)

    def test_corrupt_or_unbound_cache_is_rejected(self):
        self.prepare()
        entry = next(self.cache.iterdir())
        (entry / "base-font.apk").write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "checksum differs"):
            self.prepare()
        self.assertEqual(self.patcher.call_count, 1)
        (entry / "manifest.json").unlink()
        with self.assertRaisesRegex(ValueError, "Invalid font cache"):
            self.prepare()

    def test_cache_font_mismatch_or_package_mismatch_is_rejected(self):
        self.prepare()
        self.verify.side_effect = ValueError("font mismatch")
        with self.assertRaisesRegex(ValueError, "font mismatch"):
            self.prepare()
        self.verify.side_effect = None
        self.metadata.side_effect = [self.info, {**self.info, "versionCode": "406"}]
        with self.assertRaisesRegex(ValueError, "package metadata"):
            self.prepare()

    def test_unvalidated_generation_is_never_published(self):
        self.verify.side_effect = ValueError("font mismatch")
        with self.assertRaisesRegex(ValueError, "font mismatch"):
            self.prepare()
        self.assertFalse(self.cache.exists())

    def test_no_flag_preserves_uncached_behavior(self):
        self.assertFalse(prepare_font_apk(self.base, self.font, self.destination, None, "aapt", {}))
        self.assertFalse(self.cache.exists())
        self.patcher.assert_called_once()

    def test_independent_font_has_separate_cache_and_preservation_verification(self):
        self.prepare()
        self.assertFalse(prepare_font_apk(self.base, self.font, self.destination,
                                         self.cache, "aapt", {}, "add-font"))
        self.assertTrue(prepare_font_apk(self.base, self.font, self.destination,
                                        self.cache, "aapt", {}, "add-font"))
        self.assertEqual(len(list(self.cache.glob("*/manifest.json"))), 2)
        self.patcher.assert_called_with(self.base, self.font, self.destination, mode="add-font")
        self.verify.assert_called_with(self.destination, self.font, "add-font", self.base)


class ModuleFontTests(unittest.TestCase):
    def test_add_font_signed_output_requires_matching_module_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module, wrapped = root / "module.apk", root / "wrapped.apk"
            font = root / "requested.otf"
            font.write_bytes(b'requested font')
            with ZipFile(module, "w") as archive:
                archive.writestr("assets/idoly-localify/dict.json", json.dumps(
                    {"font": True, "font_mode": "replace-ui"}))
            def wrap():
                with ZipFile(wrapped, "w") as archive:
                    archive.writestr(f"assets/lspatch/modules/{MODULE_PACKAGE}.apk", module.read_bytes())
                    archive.writestr("assets/lspatch/origin.apk", b'legacy add-font APK')
            wrap()
            with patch("build_test_apks.verify_font") as embedded:
                with self.assertRaisesRegex(ValueError, "standalone font bundle"):
                    verify_embedded(wrapped, module, font, "add-font")
                embedded.assert_not_called()
            with ZipFile(module, "a") as archive:
                archive.writestr("assets/idoly-localify/resource-han-rounded.bundle", b'bundle')
            wrap()
            with patch("patch_font.font_bytes", return_value=b'requested font'), \
                    patch("build_font_bundle.verify") as bundle, \
                    patch("build_test_apks.verify_font") as embedded:
                verify_embedded(wrapped, module, font, "add-font")
                bundle.assert_called_once_with(b'bundle', b'requested font')
                embedded.assert_called_once()
                embedded.reset_mock()
                bundle.side_effect = ValueError('Embedded font does not match requested font byte-for-byte')
                with self.assertRaisesRegex(ValueError, 'does not match requested font'):
                    verify_embedded(wrapped, module, font, "add-font")
                embedded.assert_not_called()

    def test_missing_module_bundle_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / "module.apk"
            with ZipFile(module, "w") as archive:
                archive.writestr("other", "no font")
            with self.assertRaisesRegex(ValueError, "standalone font bundle"):
                verify_module_font_bundle(module, Path(directory) / "font.otf")

    def test_module_mode_rejects_changed_game_font_resource(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module, original, wrapped = (root / n for n in ("module.apk", "original.apk", "wrapped.apk"))
            with ZipFile(module, "w") as archive:
                archive.writestr("assets/idoly-localify/dict.json", json.dumps({"font": True, "font_mode": "replace-ui"}))
            with ZipFile(original, "w") as archive:
                archive.writestr("font-data", b"original fonts")
            def wrap(payload):
                buffer = BytesIO()
                with ZipFile(buffer, "w") as archive:
                    archive.writestr("font-data", payload)
                with ZipFile(wrapped, "w") as archive:
                    archive.writestr(f"assets/lspatch/modules/{MODULE_PACKAGE}.apk", module.read_bytes())
                    archive.writestr("assets/lspatch/origin.apk", buffer.getvalue())
            with patch("build_test_apks.verify_module_font_bundle"), patch("patch_font.split_names", return_value=["font-data"]):
                wrap(b"original fonts")
                verify_embedded(wrapped, module, root / "font.otf", "module-font", original)
                wrap(b"changed fonts")
                with self.assertRaisesRegex(ValueError, "changed the original font"):
                    verify_embedded(wrapped, module, root / "font.otf", "module-font", original)


class FontPolicyTests(unittest.TestCase):
    def test_runtime_modes_require_their_actual_payload(self):
        self.assertEqual(DEFAULT_PATCH_MODE, "module-font")
        for mode in ("fallback", "replace-translated", "replace-all"):
            self.assertEqual(required_patch_mode(mode), "replace-fot")
            validate_font_pair(mode, "replace-fot")
            with self.assertRaisesRegex(ValueError, "requires APK font payload"):
                validate_font_pair(mode, "add-font")
        validate_font_pair("replace-ui", "add-font")
        for mode in ("replace-ui", "preserve-non-cjk"):
            self.assertEqual(required_patch_mode(mode), "module-font")
            validate_font_pair(mode, "module-font")
        for mode in ("fallback", "replace-translated", "replace-all"):
            with self.assertRaisesRegex(ValueError, "requires APK font payload"):
                validate_font_pair(mode, "module-font")
        with self.assertRaisesRegex(ValueError, "requires APK font payload"):
            validate_font_pair("replace-ui", "replace-fot")
        validate_font_pair("preserve-non-cjk", "add-font")
        with self.assertRaisesRegex(ValueError, "requires APK font payload"):
            validate_font_pair("preserve-non-cjk", "replace-fot")
        validate_font_pair("replace-all", "add-font", False)
        with self.assertRaisesRegex(ValueError, "Unsupported runtime"):
            validate_font_pair("unknown", "add-font", False)

    def test_module_dictionary_font_contract_is_verified(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / "module.apk"
            def write(mode):
                with ZipFile(module, "w") as package:
                    package.writestr("assets/idoly-localify/dict.json", json.dumps(
                        {"font": True, "font_mode": mode}))
            write("replace-ui")
            verify_module_font_policy(module, "add-font")
            write("preserve-non-cjk")
            verify_module_font_policy(module, "add-font")
            with self.assertRaisesRegex(ValueError, "requires APK font payload"):
                verify_module_font_policy(module, "replace-fot")
            write("replace-all")
            verify_module_font_policy(module, "replace-fot")
            with self.assertRaisesRegex(ValueError, "requires APK font payload"):
                verify_module_font_policy(module, "add-font")
            with ZipFile(module, "w") as package:
                package.writestr("unrelated.txt", "no dictionary")
            with self.assertRaisesRegex(ValueError, "must contain a valid"):
                verify_module_font_policy(module, "add-font")


if __name__ == "__main__":
    unittest.main()
