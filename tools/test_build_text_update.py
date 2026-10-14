"""Checks release packaging boundaries and the manifest publication order."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import build_text_update as release


class BuildTextUpdateTests(unittest.TestCase):
    NOTICE_PATH = "/notice/" + "a" * 64 + "/index.html"

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "project"
        self.output = self.root / ".analysis/release"
        self.asset_dir = self.root / "app/src/main/assets/idoly-localify"
        self.asset_dir.mkdir(parents=True)
        self.dictionary = {"dump": False, "font": True, "font_mode": "replace-all",
                           "i18n": {}, "i18n_by_source": {}, "text": {},
                           "translated_values": []}
        self.notice = {"host": "stat.game-idolypride.jp", "pages": {}}
        self.write_asset("dict.json", self.dictionary)
        self.write_asset("web-notice.json", self.notice)
        for name in release.CATALOG_NAMES:
            self.write_asset(name, {"schema_version": 1, "stories": []})

    def write_asset(self, name, value):
        (self.asset_dir / name).write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    def build(self, **kwargs):
        with patch.object(release.Pipeline, "run", return_value={}) as verify:
            manifest = release.build(self.root, self.output,
                                     "https://example.org/idoly/", **kwargs)
        verify.assert_called_once_with(verify=True)
        return manifest

    def test_packages_only_verified_text_json_with_matching_urls_and_hashes(self):
        manifest = self.build()
        self.assertEqual(set(manifest["files"]), {"dict.json", "web-notice.json"})
        self.assertEqual(json.loads((self.output / "manifest.json").read_text()), manifest)
        for name, entry in manifest["files"].items():
            digest = hashlib.sha256((self.asset_dir / name).read_bytes()).hexdigest()
            self.assertEqual(entry["sha256"], digest)
            self.assertEqual(entry["url"], f"https://example.org/idoly/objects/{digest}/{name}")
            self.assertEqual((self.output / "objects" / digest / name).read_bytes(),
                             (self.asset_dir / name).read_bytes())
        self.assertEqual(manifest, self.build())

    def test_github_zip_schema_and_deterministic_assets(self):
        from zipfile import ZipFile
        manifest = self.build(github_repository="DreamGallery/Idoly-localify-translations")
        self.assertEqual(manifest["schema_version"], 2)
        self.assertNotIn("url", manifest["files"]["dict.json"])
        archive = self.output / "text-update.zip"
        original = archive.read_bytes()
        self.assertEqual(hashlib.sha256(original).hexdigest(), manifest["archive"]["sha256"])
        self.assertIn("/releases/download/text-" + manifest["version"], manifest["archive"]["url"])
        with ZipFile(archive) as package:
            self.assertEqual(set(package.namelist()), set(manifest["files"]))
            for name in package.namelist():
                self.assertEqual(hashlib.sha256(package.read(name)).hexdigest(), manifest["files"][name]["sha256"])
        self.assertEqual(self.build(github_repository="DreamGallery/Idoly-localify-translations"), manifest)
        self.assertEqual(archive.read_bytes(), original)

    def test_master_blob_requires_valid_source_receipt(self):
        with self.assertRaisesRegex(ValueError, "master-tag"):
            self.build(include_master_blobs=True)
        with patch("master_blob_patches.verify_patches", side_effect=ValueError("stale blobs")):
            with self.assertRaisesRegex(ValueError, "stale blobs"):
                self.build(include_master_blobs=True, master_tag=self.root / "tag.pb")

    def test_automatic_capture_cannot_be_published(self):
        self.dictionary["dump"] = True
        self.write_asset("dict.json", self.dictionary)
        with self.assertRaisesRegex(ValueError, "capture must be disabled"):
            self.build()

    def test_notice_is_optional(self):
        (self.asset_dir / "web-notice.json").unlink()
        self.assertEqual(set(self.build()["files"]), {"dict.json"})
        self.write_asset("web-notice.json", self.notice)
        self.assertEqual(set(self.build(include_notice=False)["files"]), {"dict.json"})

    def test_new_font_policy_is_preserved_and_unknown_policy_is_rejected(self):
        self.dictionary["font_mode"] = "replace-ui"
        self.write_asset("dict.json", self.dictionary)
        manifest = self.build()
        digest = manifest["files"]["dict.json"]["sha256"]
        value = json.loads((self.output / "objects" / digest / "dict.json").read_text())
        self.assertEqual(value["font_mode"], "replace-ui")
        self.dictionary["font_mode"] = "replace-ui-typo"
        self.write_asset("dict.json", self.dictionary)
        with self.assertRaises(ValueError):
            self.build()

    def test_notice_release_accepts_full_archive_scale_and_rejects_over_32mib(self):
        pages = {
            f"/notice/{page:064x}/index.html": {
                f"{entry}-{page}-" + "日" * 225: "中"
                for entry in range(10)
            }
            for page in range(3400)
        }
        self.notice["pages"] = pages
        self.write_asset("web-notice.json", self.notice)
        size = (self.asset_dir / "web-notice.json").stat().st_size
        self.assertGreater(size, 20 * 1024 * 1024)
        self.assertLess(size, 32 * 1024 * 1024)
        manifest = self.build()
        self.assertIn("web-notice.json", manifest["files"])
        (self.asset_dir / "web-notice.json").write_bytes(b"x" * (32 * 1024 * 1024 + 1))
        with self.assertRaisesRegex(ValueError, "download limit"):
            self.build()

    def test_notice_rejects_malformed_nested_mapping(self):
        bad_pages = [
            {self.NOTICE_PATH: {}},
            {self.NOTICE_PATH: {"原文": 42}},
            {self.NOTICE_PATH: {"": "译文"}},
            {self.NOTICE_PATH: {"原文": ""}},
            {"/notice/invalid/index.html": {"原文": "译文"}},
            {self.NOTICE_PATH: {str(i): "中" for i in range(2049)}},
            {self.NOTICE_PATH: {"原文": "中" * (64 * 1024)}},
            {self.NOTICE_PATH: {str(i) + "日" * 220: "中" for i in range(1800)}},
        ]
        for pages in bad_pages:
            with self.subTest(first=list(pages)[:1]):
                self.notice["pages"] = pages
                self.write_asset("web-notice.json", self.notice)
                with self.assertRaisesRegex(ValueError, "web-notice.json"):
                    self.build()

    def test_legal_domain_is_packaged_and_rejects_conflicting_bodies(self):
        self.dictionary['legal_by_type'] = {'1': {'source': '規約', 'translation': '条款'}}
        self.write_asset('dict.json', self.dictionary)
        self.assertIn('dict.json', self.build()['files'])
        self.dictionary['legal_by_type']['2'] = {'source': '規約', 'translation': '其他'}
        self.write_asset('dict.json', self.dictionary)
        with self.assertRaisesRegex(ValueError, 'conflicting legal'):
            self.build()

    def test_numeric_template_is_packaged_only_when_strictly_valid(self):
        source = '{number}日目のログインボーナスです'
        self.dictionary['numeric_text_templates'] = {
            source: '这是第{number}天的登录奖励'}
        self.write_asset('dict.json', self.dictionary)
        self.assertIn('dict.json', self.build()['files'])
        invalid = [
            {source: '这是第{user}天的登录奖励'},
            {'{number}！': '第{number}天'},
            {source: '这是第{number}{number}天的登录奖励'},
            {f'条目{i}' + source: f'对应{i}{{number}}天的登录奖励'
             for i in range(129)},
        ]
        for templates in invalid:
            with self.subTest(size=len(templates)):
                self.dictionary['numeric_text_templates'] = templates
                self.write_asset('dict.json', self.dictionary)
                with self.assertRaisesRegex(ValueError, 'numeric text template'):
                    self.build()

    def test_home_activity_names_are_optional_and_strictly_validated(self):
        self.dictionary['home_activity_names'] = {'サイン会': '签名会'}
        self.write_asset('dict.json', self.dictionary)
        self.assertIn('dict.json', self.build()['files'])
        for names in ([], {'サイン会': '{user}'}, {'サイン会 Lv28': '签名会'},
                      {'A': 'B', 'B': 'C'}, {str(i): '译名' for i in range(65)}):
            self.dictionary['home_activity_names'] = names
            with self.assertRaisesRegex(ValueError, 'home activity name'):
                release.validate_json('dict.json', json.dumps(self.dictionary).encode())

    def test_includes_every_verified_story_under_resource(self):
        story_dir = self.root.parent / "HoshimiToolkit/cache/local-files/resource"
        story_dir.mkdir(parents=True)
        content = {"adv_main_01.txt": "剧情一", "adv_campaign_01-02.txt": "剧情二"}
        for name, value in content.items():
            (story_dir / name).write_text(value, encoding="utf-8")
        with patch.object(release.Pipeline, "names", return_value=[name[:-4] for name in content]):
            manifest = self.build(include_stories=True)
        self.assertEqual(set(manifest["files"]),
                         {"dict.json", "web-notice.json", *(f"resource/{name}" for name in content)})
        for name in content:
            key = f"resource/{name}"
            digest = hashlib.sha256((story_dir / name).read_bytes()).hexdigest()
            self.assertEqual(manifest["files"][key], {
                "url": f"https://example.org/idoly/objects/{digest}/{key}",
                "sha256": digest,
            })
            self.assertEqual((self.output / "objects" / digest / key).read_bytes(),
                             (story_dir / name).read_bytes())

    def test_rejects_unsafe_or_oversize_story_without_manifest(self):
        story_dir = self.root.parent / "HoshimiToolkit/cache/local-files/resource"
        story_dir.mkdir(parents=True)
        with patch.object(release.Pipeline, "names", return_value=["adv_bad/name"]):
            with self.assertRaisesRegex(ValueError, "unsafe"):
                self.build(include_stories=True)
        (story_dir / "adv_big.txt").write_text("too large", encoding="utf-8")
        with patch.object(release.Pipeline, "names", return_value=["adv_big"]), \
                patch.object(release, "MAX_STORY", 2):
            with self.assertRaisesRegex(ValueError, "download limit"):
                self.build(include_stories=True)
        self.assertFalse((self.output / "manifest.json").exists())

    def test_failed_verification_keeps_existing_manifest(self):
        self.output.mkdir(parents=True)
        (self.output / "manifest.json").write_text("previous")
        with patch.object(release.Pipeline, "run", side_effect=ValueError("stale")):
            with self.assertRaisesRegex(ValueError, "stale"):
                release.build(self.root, self.output, "https://example.org/idoly")
        self.assertEqual((self.output / "manifest.json").read_text(), "previous")
        self.assertFalse((self.output / "objects").exists())

    def test_rejects_non_https_url_and_unexpected_or_oversize_asset(self):
        for url in ("http://example.org/release", "https://user:pass@example.org/release",
                    "https://example.org/release?token=x"):
            with self.subTest(url=url), self.assertRaises(ValueError):
                release.build(self.root, self.output, url)
        self.write_asset("dict.json", {"api_key": "private"})
        with patch.object(release.Pipeline, "run", return_value={}):
            with self.assertRaisesRegex(ValueError, "schema"):
                release.build(self.root, self.output, "https://example.org/release")
        self.assertFalse((self.output / "manifest.json").exists())

    def test_rejects_output_path_escape_and_symlink(self):
        with self.assertRaisesRegex(ValueError, "child"):
            release.validated_output(self.root, self.root / "../outside")
        self.output.parent.mkdir(parents=True)
        self.output.symlink_to(self.asset_dir, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            self.build()


if __name__ == "__main__":
    unittest.main()
