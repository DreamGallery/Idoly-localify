import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release_latest as latest
import release_translations as publisher
from release_followup import TEXT_ASSETS


class FakeReleases:
    repository = 'owner/translations'

    def __init__(self, output, tag, paths=None):
        self.output, self.tag = output, tag
        self.items = {tag: {'id': 1, 'tag_name': tag, 'draft': False, 'prerelease': False,
                            'body': 'Version notes', 'immutable': True,
                            'assets': [self.asset(path) for path in
                                       (paths if paths is not None else [output / n for n in TEXT_ASSETS])]}}
        self.operations = []
        self.latest = None

    @staticmethod
    def asset(path):
        return {'name': path.name, 'size': path.stat().st_size,
                'digest': 'sha256:' + latest.sha256(path)}

    def request(self, method, path, data=None, missing_ok=False):
        self.operations.append((method, path, copy.deepcopy(data)))
        if method == 'GET':
            if path.startswith('releases/tags/'):
                return copy.deepcopy(self.items.get(path.removeprefix('releases/tags/')))
            if path == 'releases/latest':
                return copy.deepcopy(self.items.get(self.latest))
        if method == 'PATCH' and path.startswith('releases/'):
            item = next(r for r in self.items.values() if r['id'] == int(path.split('/')[-1]))
            if data != {'make_latest': 'true'}:
                raise AssertionError('Must not edit notes, assets or version tags')
            self.latest = item['tag_name']
            return copy.deepcopy(item)
        raise AssertionError((method, path, data))


class LatestReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.tag = 'text-0123456789abcdef'
        archive = self.output / 'text-update.zip'
        archive.write_bytes(b'validated text archive')
        self.manifest = {'schema_version': 2, 'version': self.tag[5:],
                         'archive': {'url': f'https://github.com/owner/translations/releases/download/{self.tag}/text-update.zip',
                                     'sha256': latest.sha256(archive)}}
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        self.client = FakeReleases(self.output, self.tag)

    def publish(self, dry_run=False):
        return latest.publish_latest(self.client.repository, self.output, self.tag, dry_run,
                                     client=self.client)

    def test_dry_run_never_accesses_github(self):
        self.assertFalse(self.publish(True)['published'])
        self.assertEqual(self.client.operations, [])

    def test_marks_version_without_creating_alias_or_changing_assets_and_notes(self):
        before = copy.deepcopy(self.client.items)
        self.assertEqual(self.publish()['tag'], self.tag)
        self.assertEqual(self.client.latest, self.tag)
        self.assertEqual(self.client.items, before)
        writes = [op for op in self.client.operations if op[0] != 'GET']
        self.assertEqual(writes, [('PATCH', 'releases/1', {'make_latest': 'true'})])
        self.client.operations.clear()
        self.publish()
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_catalogs_must_be_verified_before_latest_moves(self):
        from story_catalog_payloads import CATALOG_NAMES
        catalogs = {}
        for name in CATALOG_NAMES:
            path = self.output / name
            path.write_text(json.dumps({'schema_version': 1, 'stories': []}))
            catalogs[name] = {'sha256': latest.sha256(path), 'url':
                f'https://github.com/{self.client.repository}/releases/download/{self.tag}/{name}'}
        self.manifest['story_catalogs'] = catalogs
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        self.client = FakeReleases(self.output, self.tag)
        with self.assertRaisesRegex(ValueError, 'not complete'):
            self.publish()
        self.assertIsNone(self.client.latest)
        self.client.items[self.tag]['assets'] += [self.client.asset(self.output / name) for name in CATALOG_NAMES]
        self.publish()
        self.assertEqual(self.client.latest, self.tag)
        self.manifest['story_catalogs']['card-stories.json']['url'] = 'https://example.org/other-version.json'
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'same immutable'):
            self.publish()

    def test_missing_draft_prerelease_or_changed_asset_stops_before_marking(self):
        good = copy.deepcopy(self.client.items[self.tag])
        for change in ({'draft': True}, {'prerelease': True}, {'assets': []},
                       {'assets': [{'name': 'manifest.json'}, {'name': 'text-update.zip'}]}):
            with self.subTest(change=change):
                self.client.items[self.tag] = {**good, **change}
                self.client.operations.clear()
                with self.assertRaisesRegex(ValueError, 'not complete'):
                    self.publish()
                self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))
        del self.client.items[self.tag]
        with self.assertRaisesRegex(ValueError, 'not complete'):
            self.publish()

    def test_mutable_archive_url_stops_before_network(self):
        self.manifest['archive']['url'] = self.manifest['archive']['url'].replace(self.tag, 'latest')
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'versioned'):
            self.publish()
        self.assertEqual(self.client.operations, [])

    def test_failed_mark_can_retry_without_republishing_assets(self):
        request = self.client.request
        def failing(method, *args, **kwargs):
            if method == 'PATCH':
                raise RuntimeError('mark failed')
            return request(method, *args, **kwargs)
        before = copy.deepcopy(self.client.items)
        with patch.object(self.client, 'request', side_effect=failing):
            with self.assertRaisesRegex(RuntimeError, 'mark failed'):
                self.publish()
        self.assertIsNone(self.client.latest)
        self.assertEqual(self.client.items, before)
        self.publish()
        self.assertEqual(self.client.latest, self.tag)

    def test_ignored_github_marker_change_is_not_reported_as_success(self):
        self.client.items['v0.7.0'] = {'id': 2, 'tag_name': 'v0.7.0'}
        self.client.latest = 'v0.7.0'
        request = self.client.request
        def ignored(method, *args, **kwargs):
            return None if method == 'PATCH' else request(method, *args, **kwargs)
        with patch.object(self.client, 'request', side_effect=ignored):
            with self.assertRaisesRegex(ValueError, 'did not select'):
                self.publish()

    def test_failed_marker_never_emits_cleanup_output(self):
        target = self.output / 'github-output'
        with patch.dict(publisher.os.environ, {'GITHUB_OUTPUT': str(target)}), \
                patch.object(publisher, 'publish', return_value={'tag': self.tag, 'published': True}), \
                patch.object(latest, 'publish_latest', side_effect=RuntimeError('mark failed')):
            with self.assertRaisesRegex(RuntimeError, 'mark failed'):
                publisher.publish_built_assets('owner/translations', '0.2.5', None, None,
                                               self.output, self.tag[5:], False, plugin_repository=None)
        self.assertFalse(target.exists())

    def test_complete_version_emits_version_tag_for_retention(self):
        target = self.output / 'github-output'
        with patch.dict(publisher.os.environ, {'GITHUB_OUTPUT': str(target)}), \
                patch.object(publisher, 'publish', return_value={'tag': self.tag, 'unchanged': True}), \
                patch.object(latest, 'publish_latest', return_value={'tag': self.tag, 'published': True}):
            publisher.publish_built_assets('owner/translations', '0.2.5', None, None,
                                           self.output, self.tag[5:], False, plugin_repository=None)
        self.assertEqual(target.read_text(), f'text_tag={self.tag}\n')


if __name__ == '__main__':
    unittest.main()
