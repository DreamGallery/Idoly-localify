"""Exercise catalog update boundaries and backward-compatible release packaging."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

import build_text_update as release
from story_catalog_payloads import CATALOG_NAMES, validate_catalog, release_catalog_assets
from release_followup import is_text
from frozen_release import cache_assets, cached_assets


class StoryCatalogPayloadTests(unittest.TestCase):
    def row(self, kind):
        group = 'card' if kind == 'card' else 'group'
        return {'file': f'adv_{kind}_test_01.txt', 'episode': 1, 'title': '标题',
                group + '_id': 'test', group + '_name': '名称'}

    def test_rejects_cross_category_short_duplicates_and_invalid_fields(self):
        for kind in ('card', 'event', 'love'):
            name = kind + '-stories.json'
            row = self.row(kind)
            value = {'schema_version': 1, 'stories': [row]}
            self.assertEqual(validate_catalog(name, json.dumps(value).encode()), value)
            invalid = [
                {**row, 'file': '../adv_card_test.txt'},
                {**row, 'file': 'adv_main_test.txt'},
                {**row, 'file': f'adv_{kind}_test_short.txt'},
                {**row, 'episode': True}, {**row, 'episode': '1'},
                {**row, 'episode': 21}, {**row, 'title': None},
                {**row, 'character_name': []},
            ]
            for record in invalid:
                with self.subTest(kind=kind, record=record), self.assertRaises(ValueError):
                    validate_catalog(name, json.dumps({'schema_version': 1, 'stories': [record]}).encode())
            with self.assertRaises(ValueError):
                validate_catalog(name, json.dumps({'schema_version': 1, 'stories': [row, row]}).encode())

    def test_catalogs_share_version_and_frozen_cache_without_changing_legacy_zip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'project'
            assets = root / 'app/src/main/assets/idoly-localify'
            assets.mkdir(parents=True)
            dictionary = {'dump': False, 'font': True, 'font_mode': 'replace-ui',
                          'i18n': {}, 'i18n_by_source': {}, 'text': {}, 'translated_values': []}
            (assets / 'dict.json').write_text(json.dumps(dictionary))
            for name in CATALOG_NAMES:
                (assets / name).write_text(json.dumps({'schema_version': 1,
                    'stories': [self.row(name.split('-')[0])]}))
            stories = root.parent / 'HoshimiToolkit/cache/local-files/resource'
            stories.mkdir(parents=True)
            (stories / 'adv_card_test_01.txt').write_text('故事')
            output = root / '.analysis/release'
            with patch.object(release.Pipeline, 'run', return_value={}), \
                    patch.object(release.Pipeline, 'names', return_value=['adv_card_test_01']):
                first = release.build(root, output, 'https://example.org', include_stories=True,
                                      github_repository='owner/repo')
                archive = (output / 'text-update.zip').read_bytes()
                self.assertEqual(set(first['story_catalogs']), set(CATALOG_NAMES))
                with ZipFile(output / 'text-update.zip') as package:
                    self.assertEqual(set(package.namelist()), set(first['files']))
                    self.assertFalse(set(package.namelist()) & set(CATALOG_NAMES))
                paths = release_catalog_assets(output, 'owner/repo', 'text-' + first['version'])
                self.assertEqual([p.name for p in paths], list(CATALOG_NAMES))
                hashes = cache_assets(root / '.analysis', first['version'], output)
                self.assertEqual(set(hashes), {'manifest.json', 'text-update.zip', *CATALOG_NAMES})
                cached_assets(root / '.analysis', {'text_version': first['version'], 'assets': hashes})
                card = assets / 'card-stories.json'
                value = json.loads(card.read_text())
                value['stories'][0]['title'] = '校对后标题'
                card.write_text(json.dumps(value))
                second = release.build(root, output, 'https://example.org', include_stories=True,
                                       github_repository='owner/repo')
                self.assertNotEqual(first['version'], second['version'])
                self.assertEqual(archive, (output / 'text-update.zip').read_bytes())
                (output / 'card-stories.json').write_text('{}')
                with self.assertRaises(ValueError):
                    release_catalog_assets(output)

    def test_retention_recognizes_complete_old_and_new_release_shapes(self):
        def record(names):
            return {'tag_name': 'text-' + 'a' * 16, 'assets': [{'name': n} for n in names]}
        base = ['manifest.json', 'text-update.zip']
        self.assertTrue(is_text(record(base)))
        self.assertTrue(is_text(record(base + list(CATALOG_NAMES))))
        self.assertFalse(is_text(record(base + ['card-stories.json'])))
        self.assertFalse(is_text(record(base + ['other.json'])))


if __name__ == '__main__':
    unittest.main()
