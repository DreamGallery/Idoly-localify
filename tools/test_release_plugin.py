import copy
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release_plugin as plugin
import release_translations as publisher
from test_release_latest import FakeReleases


class PluginReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.tag = 'v0.7.0'
        self.apk = self.directory / 'idoly-localify-0.7.0.apk'
        self.apk.write_bytes(b'verified signed plugin')
        self.client = FakeReleases(self.directory, self.tag, [self.apk])
        self.client.repository = 'owner/plugin'

    def publish(self, **kwargs):
        return plugin.publish_latest_plugin(self.client.repository, self.tag, client=self.client, **kwargs)

    def test_version_is_verified_and_marked_without_alias_or_reupload(self):
        before = copy.deepcopy(self.client.items)
        self.assertEqual(self.publish(apk=self.apk)['tag'], self.tag)
        self.assertEqual(self.client.latest, self.tag)
        self.assertEqual(self.client.items, before)
        self.assertEqual([op for op in self.client.operations if op[0] != 'GET'],
                         [('PATCH', 'releases/1', {'make_latest': 'true'})])
        self.client.operations.clear()
        self.publish()
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_mismatched_local_apk_never_changes_latest(self):
        self.apk.write_bytes(b'different build')
        with self.assertRaisesRegex(ValueError, 'differs'):
            self.publish(apk=self.apk)
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_dry_run_no_requests(self):
        self.assertFalse(self.publish(dry_run=True)['published'])
        self.assertEqual(self.client.operations, [])

    def test_incomplete_plugin_never_becomes_latest(self):
        good = copy.deepcopy(self.client.items[self.tag])
        for change in ({'draft': True}, {'prerelease': True}, {'assets': []},
                       {'assets': [{'name': self.apk.name, 'size': 100, 'digest': None}]}):
            with self.subTest(change=change):
                self.client.items[self.tag] = {**good, **change}
                with self.assertRaisesRegex(ValueError, 'checksummed APK'):
                    self.publish()
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_unchanged_text_still_marks_plugin_after_publication_and_on_retry(self):
        for apk in (self.apk, None):
            events = []
            def published(*args, **kwargs):
                events.append('plugin')
                return {'tag': self.tag, 'published': True}
            with patch.object(publisher, 'publish', side_effect=published), \
                    patch.object(publisher, 'release_output'), \
                    patch.object(publisher, 'publish_latest_plugin',
                                 side_effect=lambda *a, **k: events.append('latest')):
                publisher.publish_plugin_update('owner/plugin', '0.7.0', apk, 'Notes', False)
            self.assertEqual(events, ['plugin', 'latest'] if apk else ['latest'])

    def test_token_is_scoped_and_restored_even_after_failure(self):
        with patch.dict(os.environ, {'GH_TOKEN': 'text-token', 'PLUGIN_RELEASE_TOKEN': 'plugin-token'}, clear=True):
            with self.assertRaises(RuntimeError):
                with plugin.plugin_auth():
                    self.assertEqual(os.environ['GH_TOKEN'], 'plugin-token')
                    raise RuntimeError('failed')
            self.assertEqual(os.environ['GH_TOKEN'], 'text-token')
        with patch.dict(os.environ, {'GITHUB_ACTIONS': 'true', 'GH_TOKEN': 'text-token'}, clear=True):
            with self.assertRaisesRegex(ValueError, 'PLUGIN_RELEASE_TOKEN'):
                with plugin.plugin_auth():
                    self.fail('must not use text token to publish plugin')

    def test_plugin_and_text_use_separate_repositories_commits_and_tokens(self):
        calls = []
        def publish(repo, tag, *args, **kwargs):
            calls.append((repo, tag, kwargs['target_commit'], os.environ['GH_TOKEN']))
            return {'tag': tag, 'published': True}
        with patch.dict(os.environ, {'GH_TOKEN': 'text-token', 'PLUGIN_RELEASE_TOKEN': 'plugin-token'}, clear=True), \
                patch.object(publisher, 'publish', side_effect=publish), \
                patch.object(publisher, 'publish_latest_plugin') as plugin_latest, \
                patch('release_latest.publish_latest') as text_latest, \
                patch('story_catalog_payloads.release_catalog_assets', return_value=[]):
            publisher.publish_built_assets('owner/text', '0.7.0', self.apk, 'notes',
                self.directory, '0123456789abcdef', False, target_commit='b' * 40,
                plugin_repository='owner/plugin', plugin_commit='a' * 40)
            self.assertEqual(calls, [('owner/plugin', 'v0.7.0', 'a' * 40, 'plugin-token'),
                                    ('owner/text', 'text-0123456789abcdef', 'b' * 40, 'text-token')])
            plugin_latest.assert_called_once_with('owner/plugin', 'v0.7.0', apk=self.apk)
            self.assertEqual(text_latest.call_args.args[0], 'owner/text')


if __name__ == '__main__':
    unittest.main()
