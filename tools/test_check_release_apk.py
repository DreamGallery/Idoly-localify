"""Check the APK asset boundary independently of font and native validation."""
from io import BytesIO
import json
import unittest
import warnings
from zipfile import ZipFile

from check_release_apk import ASSETS, validate_payloads


class ReleaseApkPayloadTests(unittest.TestCase):
    def payloads(self):
        files = {name: b'fixture' for name in (
            'dict.json', 'web-notice.json', 'character-groups.json',
            'resource-han-rounded.bundle', 'love-rounded.bundle',
            'images/manifest.json', 'images/atlas-01.png')}
        for kind in ('card', 'event', 'love', 'bond'):
            group = 'card' if kind == 'card' else 'group'
            files[kind + '-stories.json'] = json.dumps({
                'schema_version': 1,
                'stories': [{'file': f'adv_{kind}_test_01.txt', 'episode': 1,
                             'title': 'Title', group + '_id': 'test',
                             group + '_name': 'Group'}],
            }).encode()
        return files

    def validate(self, files, duplicate=False):
        stream = BytesIO()
        with ZipFile(stream, 'w') as archive:
            archive.writestr('classes.dex', b'fixture')
            for name, data in files.items():
                archive.writestr(ASSETS + name, data)
            if duplicate:
                with warnings.catch_warnings():
                    warnings.simplefilter('ignore', UserWarning)
                    archive.writestr('classes.dex', b'duplicate')
        with ZipFile(stream) as archive:
            return validate_payloads(archive, ('manifest.json', 'atlas-01.png'))

    def test_complete_release_accepts_bond_and_legacy_catalogs(self):
        files = self.payloads()
        names, payloads = self.validate(files)
        self.assertEqual(payloads, set(files))
        self.assertIn(ASSETS + 'bond-stories.json', names)

    def test_missing_catalog_and_image_are_rejected(self):
        for missing in ('bond-stories.json', 'card-stories.json', 'images/atlas-01.png'):
            with self.subTest(missing=missing):
                files = self.payloads()
                del files[missing]
                with self.assertRaisesRegex(ValueError, 'missing='):
                    self.validate(files)

    def test_unreviewed_asset_is_rejected(self):
        files = self.payloads()
        files['capture.json'] = b'{}'
        with self.assertRaisesRegex(ValueError, 'unexpected=.*capture'):
            self.validate(files)

    def test_duplicate_archive_entry_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate APK entries'):
            self.validate(self.payloads(), duplicate=True)

    def test_invalid_or_cross_category_bond_catalog_is_rejected(self):
        for value in ({'schema_version': 2, 'stories': []}, {
                'schema_version': 1, 'stories': [{
                    'file': 'adv_card_test_01.txt', 'episode': 1, 'title': 'Title',
                    'group_id': 'test', 'group_name': 'Group'}]}):
            with self.subTest(value=value):
                files = self.payloads()
                files['bond-stories.json'] = json.dumps(value).encode()
                with self.assertRaises(ValueError):
                    self.validate(files)


if __name__ == '__main__':
    unittest.main()
