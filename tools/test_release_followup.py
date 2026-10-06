import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import release_followup as followup
import release_translations as release


def text_release(number, **changes):
    result = {'id': number, 'tag_name': f'text-{number:016x}', 'draft': False, 'prerelease': False,
              'published_at': f'2026-10-{number:02d}T15:00:00Z',
              'assets': [{'name': name} for name in sorted(followup.TEXT_ASSETS)]}
    result.update(changes)
    return result


class FakeGitHub:
    repository = 'owner/translations'

    def __init__(self, releases, latest):
        self.items = {r['tag_name']: copy.deepcopy(r) for r in releases}
        self.refs = {tag: {'object': {'sha': 'a' * 40}} for tag in self.items}
        self.latest = latest
        self.deleted = []
        self.fail_ref_once = False
        self.alias_manifest = None

    def releases(self):
        return list(self.items.values())

    def release_manifest(self, release):
        return self.alias_manifest

    def request(self, method, path, data=None, missing_ok=False):
        if method == 'GET':
            if path == 'releases/latest':
                return self.items[self.latest]
            if path.startswith('releases/tags/'):
                return self.items.get(path.removeprefix('releases/tags/'))
            if path.startswith('git/ref/tags/'):
                return self.refs.get(path.removeprefix('git/ref/tags/'))
        if method == 'DELETE':
            if path.startswith('releases/'):
                identity = int(path.split('/')[-1])
                tag = next(tag for tag, item in self.items.items() if item['id'] == identity)
                del self.items[tag]
            elif path.startswith('git/refs/tags/'):
                if self.fail_ref_once:
                    self.fail_ref_once = False
                    raise RuntimeError('simulated tag deletion outage')
                tag = path.removeprefix('git/refs/tags/')
                del self.refs[tag]
            else:
                raise AssertionError(path)
            self.deleted.append(path)
            return None
        raise AssertionError((method, path, data))


class CleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.journal = Path(self.temp.name) / 'cleanup.json'

    def test_policy_preserves_current_latest_plugin_draft_and_mixed_releases(self):
        releases = [text_release(n) for n in range(1, 9)]
        releases += [text_release(9, tag_name='v0.2.5'), text_release(10, draft=True),
                     text_release(11, prerelease=True),
                     text_release(12, assets=[{'name': 'idoly-localify.apk'}]),
                     text_release(13, tag_name='latest')]
        candidates, protected = followup.cleanup_plan(releases, 3, releases[0]['tag_name'], releases[1]['tag_name'])
        self.assertEqual([r['id'] for r in candidates], [5, 4, 3])
        self.assertIn('latest', protected)

    def test_preview_never_deletes_or_writes_journal(self):
        client = FakeGitHub([text_release(n) for n in range(1, 5)], text_release(4)['tag_name'])
        result = followup.cleanup(client, client.latest, 2, self.journal)
        self.assertEqual(result['would_delete'], [text_release(n)['tag_name'] for n in (1, 2)])
        self.assertEqual(client.deleted, [])
        self.assertFalse(self.journal.exists())

    def test_applied_cleanup_deletes_release_then_its_exact_tag(self):
        client = FakeGitHub([text_release(n) for n in range(1, 5)], text_release(4)['tag_name'])
        result = followup.cleanup(client, client.latest, 2, self.journal, True)
        self.assertEqual(len(result['deleted']), 2)
        self.assertEqual(client.deleted, ['releases/1', f'git/refs/tags/{text_release(1)["tag_name"]}',
                                          'releases/2', f'git/refs/tags/{text_release(2)["tag_name"]}'])
        self.assertEqual(json.loads(self.journal.read_text())['pending'], {})

    def test_tag_failure_is_recovered_from_journal_without_deleting_unrelated_orphans(self):
        client = FakeGitHub([text_release(n) for n in range(1, 4)], text_release(3)['tag_name'])
        client.fail_ref_once = True
        with self.assertRaisesRegex(RuntimeError, 'outage'):
            followup.cleanup(client, client.latest, 2, self.journal, True)
        orphan = 'text-ffffffffffffffff'
        client.refs[orphan] = {'object': {'sha': 'b' * 40}}
        result = followup.cleanup(client, client.latest, 2, self.journal, True)
        self.assertEqual(result['deleted'], [text_release(1)['tag_name']])
        self.assertIn(orphan, client.refs)

    def test_moved_tag_is_not_deleted_on_retry(self):
        client = FakeGitHub([text_release(n) for n in range(1, 4)], text_release(3)['tag_name'])
        client.fail_ref_once = True
        with self.assertRaises(RuntimeError):
            followup.cleanup(client, client.latest, 2, self.journal, True)
        client.refs[text_release(1)['tag_name']]['object']['sha'] = 'c' * 40
        with self.assertRaisesRegex(ValueError, 'Tag moved'):
            followup.cleanup(client, client.latest, 2, self.journal, True)
        self.assertIn(text_release(1)['tag_name'], client.refs)

    def test_latest_changing_to_candidate_is_protected(self):
        client = FakeGitHub([text_release(n) for n in range(1, 4)], text_release(3)['tag_name'])
        original = client.request
        count = 0

        def request(method, path, *args, **kwargs):
            nonlocal count
            if path == 'releases/latest':
                count += 1
                if count > 1:
                    client.latest = text_release(1)['tag_name']
            return original(method, path, *args, **kwargs)

        with patch.object(client, 'request', side_effect=request):
            followup.cleanup(client, text_release(3)['tag_name'], 2, self.journal, True)
        self.assertEqual(client.deleted, [])

    def test_disabled_does_not_access_github_and_invalid_current_stops(self):
        client = FakeGitHub([text_release(1)], text_release(1)['tag_name'])
        with patch.object(client, 'request') as api:
            self.assertEqual(followup.cleanup(client, 'unused', 0, self.journal, True), {'cleanup': 'disabled'})
            api.assert_not_called()
        with self.assertRaisesRegex(ValueError, 'missing or incomplete'):
            followup.cleanup(client, text_release(2)['tag_name'], 1, self.journal, True)
        self.assertFalse(client.deleted)

    def test_fixed_latest_protects_its_older_referenced_archive(self):
        client = FakeGitHub([text_release(n) for n in range(1, 5)] +
                            [text_release(5, tag_name='latest')], 'latest')
        older = text_release(1)['tag_name']
        client.alias_manifest = {'schema_version': 2, 'version': older[5:],
                                 'archive': {'url': f'https://github.com/{client.repository}/releases/download/{older}/text-update.zip'}}
        result = followup.cleanup(client, text_release(4)['tag_name'], 1, self.journal, True)
        self.assertEqual(result['deleted'], [text_release(n)['tag_name'] for n in (2, 3)])
        self.assertIn(older, client.items)
        self.assertIn('latest', client.items)

    def test_broken_latest_manifest_stops_all_cleanup(self):
        client = FakeGitHub([text_release(n) for n in range(1, 5)] +
                            [text_release(5, tag_name='latest')], 'latest')
        client.alias_manifest = {'schema_version': 2, 'version': 'bad', 'archive': {'url': 'https://other.example/'}}
        with self.assertRaisesRegex(ValueError, 'manifest target'):
            followup.cleanup(client, text_release(4)['tag_name'], 1, self.journal, True)
        self.assertFalse(client.deleted)


class DispatchAndOutputsTests(unittest.TestCase):
    def test_exact_published_plugin_is_dispatched_with_repository_defaults(self):
        source, target = unittest.mock.Mock(), unittest.mock.Mock()
        target.repository = 'owner/patcher'
        source.request.return_value = {'tag_name': 'v0.2.5', 'draft': False, 'prerelease': False,
                                       'assets': [{'name': 'idoly-localify-0.2.5.apk', 'size': 100,
                                                   'digest': 'sha256:' + 'a' * 64}]}
        followup.dispatch(source, target, 'v0.2.5')
        target.request.assert_called_once_with('POST', 'actions/workflows/patch.yml/dispatches',
                                               {'ref': 'main', 'inputs': {'module_tag': 'v0.2.5',
                                                                        'automatic': True, 'publish': True}})
        source.request.return_value['draft'] = True
        target.reset_mock()
        with self.assertRaises(ValueError):
            followup.dispatch(source, target, 'v0.2.5')
        target.request.assert_not_called()
        with self.assertRaises(ValueError):
            followup.dispatch(source, target, 'text-0123456789abcdef')

    def test_plugin_output_survives_later_text_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'github-output'
            with patch.dict(release.os.environ, {'GITHUB_OUTPUT': str(output)}), \
                    patch.object(release, 'publish', side_effect=[{'tag': 'v0.2.5', 'published': True},
                                                               RuntimeError('text failed')]):
                with self.assertRaisesRegex(RuntimeError, 'text failed'):
                    release.publish_built_assets('owner/repo', '0.2.5', Path('plugin.apk'), 'notes',
                                                 Path(temporary), '0123456789abcdef', False)
            self.assertEqual(output.read_text(), 'plugin_tag=v0.2.5\n')

    def test_dry_run_and_unchanged_plugin_never_dispatch_but_unchanged_text_can_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / 'github-output'
            with patch.dict(release.os.environ, {'GITHUB_OUTPUT': str(output)}):
                release.release_output('plugin_tag', {'tag': 'v0.2.5', 'published': True}, True)
                release.release_output('plugin_tag', {'tag': 'v0.2.5', 'unchanged': True}, False)
                self.assertFalse(output.exists())
                release.release_output('text_tag', {'tag': 'text-0123456789abcdef', 'unchanged': True}, False)
            self.assertEqual(output.read_text(), 'text_tag=text-0123456789abcdef\n')


if __name__ == '__main__':
    unittest.main()
