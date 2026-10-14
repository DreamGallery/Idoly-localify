import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release_latest as latest
import release_translations as publisher


class FakeReleases:
    repository = 'owner/translations'

    def __init__(self, output, tag):
        self.output, self.tag = output, tag
        self.items = {tag: {'id': 1, 'tag_name': tag, 'draft': False, 'prerelease': False,
                            'published_at': '2026-10-06T15:00:00Z',
                            'assets': [self.asset(output / name) for name in latest.TEXT_ASSETS]}}
        self.refs = {}
        self.operations = []
        self.uploaded = []
        self.latest = tag
        self.hide_drafts_from_tag_endpoint = False

    @staticmethod
    def asset(path):
        return {'name': path.name, 'size': path.stat().st_size,
                'digest': 'sha256:' + latest.sha256(path)}

    def releases(self):
        return copy.deepcopy(list(self.items.values()))

    def request(self, method, path, data=None, missing_ok=False):
        self.operations.append((method, path, copy.deepcopy(data)))
        if method == 'GET':
            if path.startswith('releases/tags/'):
                item = self.items.get(path.removeprefix('releases/tags/'))
                if item and item.get('draft') and self.hide_drafts_from_tag_endpoint:
                    return None
                return copy.deepcopy(item)
            if path == 'releases/latest':
                return copy.deepcopy(self.items[self.latest])
            if path.startswith('releases/'):
                return copy.deepcopy(next(r for r in self.items.values() if r['id'] == int(path.split('/')[-1])))
            if path == 'commits/' + self.tag:
                return {'sha': 'a' * 40}
            if path == 'git/ref/tags/latest':
                return copy.deepcopy(self.refs.get('latest'))
        if method == 'POST' and path == 'releases':
            self.items['latest'] = {**data, 'id': 2, 'assets': []}
            return copy.deepcopy(self.items['latest'])
        if method in ('POST', 'PATCH') and path in ('git/refs', 'git/refs/tags/latest'):
            self.refs['latest'] = {'object': {'sha': data['sha']}}
            return copy.deepcopy(self.refs['latest'])
        if method == 'PATCH' and path == 'releases/2':
            self.items['latest'].update(data)
            if data.get('make_latest') == 'true':
                self.latest = 'latest'
            return copy.deepcopy(self.items['latest'])
        raise AssertionError((method, path, data))

    def upload(self, repository, path):
        self.uploaded.append(path.name)
        self.operations.append(('UPLOAD', path.name, None))
        self.items['latest']['assets'] = [a for a in self.items['latest']['assets'] if a['name'] != path.name]
        self.items['latest']['assets'].append(self.asset(path))


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
                                     client=self.client, uploader=self.client.upload)

    def test_dry_run_never_accesses_github(self):
        self.assertFalse(self.publish(True)['published'])
        self.assertEqual(self.client.operations, [])

    def test_bootstrap_uploads_zip_then_manifest_and_only_then_publishes(self):
        self.publish()
        self.assertEqual(self.client.uploaded, ['text-update.zip', 'manifest.json'])
        self.assertFalse(self.client.items['latest']['draft'])
        self.assertEqual(self.client.latest, 'latest')
        self.assertEqual(self.client.refs['latest']['object']['sha'], 'a' * 40)
        writes = [(method, path) for method, path, _ in self.client.operations if method != 'GET']
        self.assertEqual(writes, [('POST', 'releases'), ('UPLOAD', 'text-update.zip'),
                                 ('UPLOAD', 'manifest.json'), ('POST', 'git/refs'), ('PATCH', 'releases/2')])
        self.assertEqual(json.loads((self.output / 'manifest.json').read_text()), self.manifest)

    def test_catalogs_must_be_verified_on_immutable_release_before_alias_moves(self):
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
        self.assertNotIn('latest', self.client.items)
        self.client.items[self.tag]['assets'] += [self.client.asset(self.output / name) for name in CATALOG_NAMES]
        self.publish()
        self.assertEqual(self.client.uploaded, ['text-update.zip', 'manifest.json'])
        self.manifest['story_catalogs']['card-stories.json']['url'] = 'https://example.org/other-version.json'
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, 'same immutable'):
            self.publish()

    def test_same_assets_skip_reupload_but_refresh_plugin_link(self):
        self.publish()
        self.client.items['v0.2.5'] = {
            'id': 3, 'tag_name': 'v0.2.5', 'published_at': '2026-10-06T16:00:00Z',
            'assets': [{'name': 'idoly-localify-0.2.5.apk', 'size': 10, 'digest': 'sha256:' + 'b' * 64}]}
        self.client.uploaded.clear()
        self.publish()
        self.assertEqual(self.client.uploaded, [])
        self.assertIn('/download/v0.2.5/idoly-localify-0.2.5.apk', self.client.items['latest']['body'])
        self.assertLess(self.client.items['latest']['body'].index('v0.2.5'),
                        self.client.items['latest']['body'].index('## 最新文本'))

    def test_replacing_assets_moves_only_latest_tag(self):
        self.publish()
        self.client.items['latest']['assets'][0]['digest'] = 'sha256:' + 'b' * 64
        self.client.refs['latest']['object']['sha'] = 'b' * 40
        self.client.operations.clear()
        self.publish()
        refs = [(method, path, data) for method, path, data in self.client.operations
                if method != 'GET' and path.startswith('git/')]
        self.assertEqual(refs, [('PATCH', 'git/refs/tags/latest', {'sha': 'a' * 40, 'force': True})])

    def test_failed_zip_keeps_previous_manifest_and_retry_resumes(self):
        self.publish()
        old = copy.deepcopy(self.client.items['latest'])
        self.client.items['latest']['assets'][0]['digest'] = 'sha256:' + 'b' * 64
        with patch.object(self.client, 'upload', side_effect=RuntimeError('network failure')):
            with self.assertRaisesRegex(RuntimeError, 'network'):
                self.publish()
        self.assertEqual(self.client.items['latest']['body'], old['body'])
        self.assertEqual(next(a for a in self.client.items['latest']['assets'] if a['name'] == 'manifest.json'),
                         next(a for a in old['assets'] if a['name'] == 'manifest.json'))
        self.publish()
        self.assertTrue(latest.verified_assets(self.client.items['latest'],
                                               [self.output / n for n in latest.TEXT_ASSETS]))

    def test_first_publication_resumes_draft_without_a_published_tag(self):
        self.client.hide_drafts_from_tag_endpoint = True
        with patch.object(self.client, 'upload', side_effect=RuntimeError('network failure')):
            with self.assertRaisesRegex(RuntimeError, 'network'):
                self.publish()
        self.assertTrue(self.client.items['latest']['draft'])
        self.publish()
        self.assertFalse(self.client.items['latest']['draft'])
        self.assertEqual(sum(method == 'POST' and path == 'releases'
                             for method, path, _ in self.client.operations), 1)

    def test_corrupt_uploaded_zip_never_changes_manifest_or_publishes_draft(self):
        original = self.client.upload

        def corrupt(repository, path):
            original(repository, path)
            self.client.items['latest']['assets'][-1]['digest'] = 'sha256:' + 'f' * 64

        with patch.object(self.client, 'upload', side_effect=corrupt):
            with self.assertRaisesRegex(ValueError, 'verification failed'):
                self.publish()
        self.assertTrue(self.client.items['latest']['draft'])
        self.assertEqual(self.client.uploaded, ['text-update.zip'])
        self.assertEqual(self.client.refs, {})

    def test_immutable_or_unrelated_latest_is_never_overwritten(self):
        self.publish()
        for mutation in ({'immutable': True}, {'assets': [{'name': 'unrelated.zip'}]}):
            with self.subTest(mutation=mutation):
                old = copy.deepcopy(self.client.items['latest'])
                self.client.items['latest'].update(mutation)
                self.client.operations.clear()
                with self.assertRaises(ValueError):
                    self.publish()
                self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))
                self.client.items['latest'] = old

    def test_incomplete_history_or_mutable_archive_url_stops_before_writing(self):
        self.client.items[self.tag]['draft'] = True
        with self.assertRaisesRegex(ValueError, 'Historical'):
            self.publish()
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))
        self.manifest['archive']['url'] = self.manifest['archive']['url'].replace(self.tag, 'latest')
        (self.output / 'manifest.json').write_text(json.dumps(self.manifest))
        self.client.operations.clear()
        with self.assertRaisesRegex(ValueError, 'historical'):
            self.publish()
        self.assertEqual(self.client.operations, [])

    def test_failed_alias_never_emits_cleanup_output(self):
        target = self.output / 'github-output'
        with patch.dict(publisher.os.environ, {'GITHUB_OUTPUT': str(target)}), \
                patch.object(publisher, 'publish', return_value={'tag': self.tag, 'published': True}), \
                patch.object(latest, 'publish_latest', side_effect=RuntimeError('alias failed')):
            with self.assertRaisesRegex(RuntimeError, 'alias failed'):
                publisher.publish_built_assets('owner/translations', '0.2.5', None, None,
                                               self.output, self.tag[5:], False)
        self.assertFalse(target.exists())

    def test_complete_alias_emits_historical_tag_for_retention(self):
        target = self.output / 'github-output'
        with patch.dict(publisher.os.environ, {'GITHUB_OUTPUT': str(target)}), \
                patch.object(publisher, 'publish', return_value={'tag': self.tag, 'unchanged': True}), \
                patch.object(latest, 'publish_latest', return_value={'tag': 'latest', 'published': True}):
            publisher.publish_built_assets('owner/translations', '0.2.5', None, None,
                                           self.output, self.tag[5:], False)
        self.assertEqual(target.read_text(), f'text_tag={self.tag}\n')


if __name__ == '__main__':
    unittest.main()
