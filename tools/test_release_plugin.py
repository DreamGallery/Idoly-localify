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
        self.client = FakeReleases.__new__(FakeReleases)
        self.client.repository = 'owner/plugin'
        self.client.output, self.client.tag = self.directory, self.tag
        self.client.items = {self.tag: {'id': 1, 'tag_name': self.tag,
            'draft': False, 'prerelease': False, 'body': 'Plugin changes',
            'assets': [self.client.asset(self.apk)]}}
        self.client.refs, self.client.operations, self.client.uploaded = {}, [], []
        self.client.latest = self.tag
        self.client.hide_drafts_from_tag_endpoint = True

    def publish(self, **kwargs):
        return plugin.publish_latest_plugin(self.client.repository, self.tag, client=self.client,
            uploader=self.client.upload, **kwargs)

    def test_version_is_verified_before_alias_is_published(self):
        self.publish(apk=self.apk)
        self.assertEqual(self.client.uploaded, ['idoly-localify.apk'])
        self.assertEqual(self.client.latest, 'latest')
        self.assertEqual(self.client.refs['latest']['object']['sha'], 'a' * 40)
        self.assertIn('/tag/v0.7.0', self.client.items['latest']['body'])
        self.assertEqual(self.client.items[self.tag]['assets'][0], self.client.asset(self.apk))
        before = copy.deepcopy(self.client.items)
        self.client.operations.clear()
        self.client.uploaded.clear()
        with patch.object(plugin, 'download') as download:
            self.publish()
            download.assert_not_called()
        self.assertEqual(self.client.items, before)
        self.assertEqual(self.client.uploaded, [])
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_upload_failure_preserves_draft_and_retry_recovers(self):
        with patch.object(self.client, 'upload', side_effect=RuntimeError('upload failed')):
            with self.assertRaisesRegex(RuntimeError, 'upload failed'):
                self.publish(apk=self.apk)
        self.assertTrue(self.client.items['latest']['draft'])
        self.assertEqual(self.client.refs, {})
        self.publish(apk=self.apk)
        self.assertFalse(self.client.items['latest']['draft'])
        self.assertEqual(sum(op[:2] == ('POST', 'releases') for op in self.client.operations), 1)

    def test_mismatched_local_apk_never_changes_latest(self):
        self.apk.write_bytes(b'different build')
        with self.assertRaisesRegex(ValueError, 'differs'):
            self.publish(apk=self.apk)
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

    def test_dry_run_no_requests_or_uploads(self):
        self.assertFalse(self.publish(dry_run=True)['published'])
        self.assertEqual(self.client.operations, [])

    def test_unrelated_alias_cannot_be_overwritten(self):
        self.publish(apk=self.apk)
        self.client.items['latest']['assets'].append({'name': 'manifest.json'})
        self.client.operations.clear()
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            self.publish(apk=self.apk)
        self.assertTrue(all(op[0] == 'GET' for op in self.client.operations))

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
                patch.object(publisher, 'publish_latest_plugin') as alias, \
                patch('release_latest.publish_latest') as text_alias, \
                patch('story_catalog_payloads.release_catalog_assets', return_value=[]):
            publisher.publish_built_assets('owner/text', '0.7.0', self.apk, 'notes',
                self.directory, '0123456789abcdef', False, target_commit='b' * 40,
                plugin_repository='owner/plugin', plugin_commit='a' * 40)
            self.assertEqual(calls, [('owner/plugin', 'v0.7.0', 'a' * 40, 'plugin-token'),
                                    ('owner/text', 'text-0123456789abcdef', 'b' * 40, 'text-token')])
            alias.assert_called_once_with('owner/plugin', 'v0.7.0', apk=self.apk)
            self.assertEqual(text_alias.call_args.args[0], 'owner/text')


if __name__ == '__main__':
    unittest.main()
