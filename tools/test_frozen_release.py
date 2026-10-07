"""Real local Git transactions for daily frozen publication; no network access."""
import csv
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import frozen_release as frozen
import release_translations as release


class FrozenBatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.remote, self.main, self.collab = [self.root / x for x in ('origin.git', 'main', 'collaboration')]
        self.git('init', '--bare', str(self.remote))
        self.git('init', '-b', 'main', str(self.main))
        self.identity(self.main)
        self.write(self.main, 'notice/zh-Hans.json', '{}\n')
        self.write(self.main, 'scripts/story_migration.py', '# trusted code\n')
        self.commit(self.main)
        self.git('remote', 'add', 'origin', str(self.remote), cwd=self.main)
        self.git('push', 'origin', 'main', cwd=self.main)
        self.git('clone', '-b', 'main', str(self.remote), str(self.collab)); self.identity(self.collab)
        self.git('checkout', '-b', 'collaboration', cwd=self.collab)
        self.base = self.git('rev-parse', 'HEAD', cwd=self.main)
        self.stage = self.root / 'stage'
    def git(self, *args, cwd=None):
        return subprocess.run(['git', *args], cwd=cwd, text=True, capture_output=True, check=True).stdout.strip()
    def identity(self, root):
        self.git('config', 'user.name', 'Fixture', cwd=root)
        self.git('config', 'user.email', 'fixture@example.invalid', cwd=root)
    def write(self, root, path, value):
        target = root / path; target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value); return target
    def commit(self, root):
        self.git('add', '.', cwd=root); self.git('commit', '-m', 'Fixture', cwd=root)
        return self.git('rev-parse', 'HEAD', cwd=root)
    def story(self, text='完成翻译', state='完成'):
        relative = 'story/human/main/adv_main_01.csv'
        stream = io.StringIO(); writer = csv.writer(stream, lineterminator='\n')
        writer.writerows([['id','name','text','trans'], ['1:text:0','','原文',text],
                          ['info','adv_main_01.txt','a' * 64,''], ['译者','','','']])
        self.write(self.collab, relative, stream.getvalue())
        record = {'schema_version': 1, 'file_id': 'adv_main_01',
                  'translation': {'state': state, 'revision': 1, 'operator_github': 'private-worker'},
                  'artifacts': {'translation_csv': {'path': relative}, 'translation_draft': {'path': 'draft'}},
                  'activity': ['private work history']}
        self.write(self.collab, 'records/adv_main_01.json', json.dumps(record))
        return relative
    def prepare(self):
        cutoff = self.commit(self.collab)
        hashes = frozen.stage_cutoff(self.main, self.collab, self.stage)
        return {'schema_version': 1, 'cutoff': cutoff, 'main_parent': self.base,
                'collaboration_baseline': hashes, 'text_version': 'a' * 16,
                'published_data': {}, 'assets': {}, 'plugin_version': '0.2.3', 'plugin_asset': None}
    def advance_main(self):
        self.git('fetch', 'origin', 'main', cwd=self.main)
        self.git('checkout', '--detach', 'FETCH_HEAD', cwd=self.main)
        self.base = self.git('rev-parse', 'HEAD', cwd=self.main)
    def test_frozen_snapshot_single_parent_commit_ignores_later_collaboration(self):
        relative = self.story()
        self.write(self.collab, 'story/drafts/translation/adv_draft.csv', 'unfinished draft')
        self.write(self.collab, 'scripts/story_migration.py', 'raise RuntimeError("untrusted")')
        receipt = self.prepare()
        self.assertEqual((self.stage / 'scripts/story_migration.py').read_text(), '# trusted code\n')
        self.story('截止之后的新编辑'); newer = self.commit(self.collab)
        self.git('push', 'origin', 'collaboration', cwd=self.collab)
        commit, paths = frozen.commit_batch(self.main, self.stage, self.base, receipt)
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-list', '--parents', '-1', commit), commit + ' ' + self.base)
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-list', '--count', self.base + '..main'), '1')
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-parse', 'collaboration'), newer)
        content = self.git('--git-dir', str(self.remote), 'show', 'main:' + relative)
        self.assertIn('完成翻译', content); self.assertNotIn('截止之后', content)
        self.assertFalse(any('drafts' in p or p.startswith('scripts/') for p in paths))
        published = self.git('--git-dir', str(self.remote), 'show', 'main:records/adv_main_01.json')
        self.assertNotIn('private', published); self.assertNotIn('draft', published)
        stored = json.loads(self.git('--git-dir', str(self.remote), 'show', 'main:' + frozen.RECEIPT))
        self.assertEqual(stored['cutoff'], receipt['cutoff'])
        self.assertEqual(self.git('status', '--porcelain', cwd=self.main), '')
    def test_machine_baseline_not_overwritten_by_unchanged_old_collaboration(self):
        self.story(); receipt = self.prepare()
        self.write(self.stage, 'notice/zh-Hans.json', '{"new":"machine"}\n')
        frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        self.story('新人工译文'); self.commit(self.collab)
        frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertEqual((self.stage / 'notice/zh-Hans.json').read_text(), '{"new":"machine"}\n')
    def test_new_incomplete_artifact_excluded_and_existing_formal_preserved_during_edit(self):
        relative = self.story(state='进行中'); receipt = self.prepare()
        self.assertFalse((self.stage / relative).exists())
        self.story(); self.commit(self.collab)
        receipt['collaboration_baseline'] = frozen.stage_cutoff(self.main, self.collab, self.stage)
        receipt['cutoff'] = self.git('rev-parse', 'HEAD', cwd=self.collab)
        frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        self.story('尚未完成的编辑', '进行中'); self.commit(self.collab)
        frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertIn('完成翻译', (self.stage / relative).read_text())
        self.assertNotIn('尚未完成', (self.stage / relative).read_text())
    def test_intentional_withdrawal_deletes_published_layer(self):
        relative = self.story(); receipt = self.prepare()
        frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        (self.collab / relative).unlink(); self.commit(self.collab)
        frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertFalse((self.stage / relative).exists())
        self.assertFalse((self.stage / 'records/adv_main_01.json').exists())
    def test_main_user_conflict_aborts_without_overwriting(self):
        self.story(); receipt = self.prepare()
        frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        self.write(self.main, 'notice/zh-Hans.json', '{"user":"main correction"}')
        self.commit(self.main)
        self.write(self.collab, 'notice/zh-Hans.json', '{"machine":"different"}'); self.commit(self.collab)
        with self.assertRaisesRegex(ValueError, 'both changed'):
            frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertIn('main correction', (self.main / 'notice/zh-Hans.json').read_text())
    def test_main_advance_after_freeze_fails_without_second_commit(self):
        self.story(); receipt = self.prepare()
        self.write(self.collab, 'README.md', 'unrelated main update'); advance = self.commit(self.collab)
        self.git('push', 'origin', 'HEAD:main', cwd=self.collab)
        with self.assertRaisesRegex(ValueError, 'Main advanced'):
            frozen.commit_batch(self.main, self.stage, self.base, receipt)
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-parse', 'main'), advance)
    def test_failed_asset_publication_resumes_same_receipt_without_new_commit(self):
        self.story(); receipt = self.prepare()
        output = self.root / 'output'; output.mkdir()
        self.write(output, 'manifest.json', '{"version":"' + 'a' * 16 + '"}')
        self.write(output, 'text-update.zip', 'immutable archive')
        receipt['assets'] = frozen.cache_assets(self.root, receipt['text_version'], output)
        commit, _ = frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        self.story('later collaboration'); self.commit(self.collab)
        with patch.object(release, 'release_exists', return_value=None), patch.object(release, 'publish_built_assets', side_effect=RuntimeError('upload failed')):
            with self.assertRaisesRegex(RuntimeError, 'upload failed'):
                release.resume_frozen_publication('owner/repo', self.main, self.root)
        with patch.object(release, 'release_exists', return_value=None), patch.object(release, 'publish_built_assets', return_value=[]) as publish, patch('builtins.print'):
            self.assertTrue(release.resume_frozen_publication('owner/repo', self.main, self.root))
        self.assertEqual(publish.call_args.kwargs['target_commit'], commit)
        self.assertEqual(publish.call_args.args[5], receipt['text_version'])
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-list', '--count', receipt['main_parent'] + '..main'), '1')
        cached = frozen.cached_assets(self.root, receipt)
        (cached / 'text-update.zip').write_text('tampered')
        with self.assertRaisesRegex(ValueError, 'missing or changed'): frozen.cached_assets(self.root, receipt)
    def test_initial_rollout_retires_main_drafts_only_after_collaboration_preserves_them(self):
        draft = 'story/drafts/translation/main/adv_work.csv'
        self.write(self.main, draft, 'old unpublished draft')
        self.base = self.commit(self.main)
        self.git('push', 'origin', 'main', cwd=self.main)
        self.story(); self.commit(self.collab)
        with self.assertRaisesRegex(ValueError, 'Preserve historical main work'):
            frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.write(self.collab, draft, 'old unpublished draft'); self.commit(self.collab)
        self.write(self.collab, draft, 'new unpublished draft'); cutoff = self.commit(self.collab)
        hashes = frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertFalse((self.stage / draft).exists())
        receipt = {'schema_version': 1, 'cutoff': cutoff, 'main_parent': self.base,
                   'collaboration_baseline': hashes, 'text_version': 'a' * 16}
        _, paths = frozen.commit_batch(self.main, self.stage, self.base, receipt)
        self.assertIn(draft, paths)
        self.assertEqual((self.collab / draft).read_text(), 'new unpublished draft')
        listed = self.git('--git-dir', str(self.remote), 'ls-tree', '-r', '--name-only', 'main')
        self.assertNotIn(draft, listed)

    def test_removed_json_does_not_leak_from_previous_private_checkout(self):
        from export_translation_sources import JSON_PATHS
        source, project = self.root / 'data', self.root / 'project'
        for directory in JSON_PATHS:
            self.write(source, directory + '/new.json', '{}')
            self.write(project, 'translations/' + directory + '/removed.json', '{"old":true}')
        release.replace_release_data(source, project)
        for directory in JSON_PATHS:
            self.assertFalse((project / 'translations' / directory / 'removed.json').exists())
            self.assertTrue((project / 'translations' / directory / 'new.json').is_file())

    def test_unchanged_data_and_content_do_not_need_a_new_batch(self):
        self.story(); receipt = self.prepare(); receipt['content_version'] = 'b' * 16
        frozen.commit_batch(self.main, self.stage, self.base, receipt); self.advance_main()
        self.write(self.main, 'README.md', 'only documentation changed'); self.commit(self.main)
        frozen.stage_cutoff(self.main, self.collab, self.stage)
        self.assertTrue(frozen.unchanged_batch(self.main, self.stage, 'b' * 16))
        self.assertFalse(frozen.unchanged_batch(self.main, self.stage, 'c' * 16))

    def test_migration_confirmation_must_match_formal_source_hash(self):
        relative = self.story()
        path = self.collab / 'records/adv_main_01.json'
        record = json.loads(path.read_text())
        record['source_change'] = {'status': 'confirmed', 'source_sha256': 'a' * 64}
        record['source_confirmation'] = {'source_sha256': 'b' * 64, 'confirmed_by': 'editor'}
        path.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'Invalid source confirmation'):
            frozen.projection(self.collab)
        record['source_confirmation']['source_sha256'] = 'a' * 64
        path.write_text(json.dumps(record))
        self.assertIn(relative, frozen.projection(self.collab)[0])
        record['source_change']['status'] = 'needs-confirmation'; path.write_text(json.dumps(record))
        self.assertNotIn(relative, frozen.projection(self.collab)[0])

    def test_text_release_creation_binds_exact_main_commit_and_rejects_wrong_tag(self):
        asset = self.write(self.root, 'text-update.zip', 'archive')
        with patch('release_followup.GitHub.request', return_value=None), patch.object(release, 'release_exists', return_value=None), patch.object(release, 'run') as command:
            release.publish('owner/repo', 'text-' + 'a' * 16, [asset], False, False, target_commit=self.base)
        create = command.call_args_list[0].args[0]
        self.assertEqual(create[create.index('--target') + 1], self.base)
        with patch('release_followup.GitHub.request', return_value={'sha': 'f' * 40}), patch.object(release, 'run') as command:
            with self.assertRaisesRegex(ValueError, 'different main commit'):
                release.publish('owner/repo', 'text-' + 'a' * 16, [asset], False, False, target_commit=self.base)
            command.assert_not_called()

if __name__ == '__main__': unittest.main()
