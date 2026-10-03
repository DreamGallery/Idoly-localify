"""Exercise real non-fast-forward races without network or force pushes."""
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import export_translation_sources as exports
import import_collaboration as imports
import release_translations as release


class ReleaseBranchRaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repository = self.root / 'checkout'
        self.remote = self.root / 'origin.git'
        self.editor = self.root / 'editor'
        self.execute = subprocess.run
        self.git('init', '--bare', str(self.remote))
        self.git('init', '-b', 'main', str(self.repository))
        self.git('config', 'user.name', 'Fixture', cwd=self.repository)
        self.git('config', 'user.email', 'fixture@example.invalid', cwd=self.repository)
        self.write(self.repository, 'notice/zh-Hans.json', '{}\n')
        self.write(self.repository, 'README.md', 'Documentation\n')
        self.git('add', '.', cwd=self.repository)
        self.git('commit', '-m', 'Initial', cwd=self.repository)
        self.git('remote', 'add', 'origin', str(self.remote), cwd=self.repository)
        self.git('push', 'origin', 'main', cwd=self.repository)
        self.git('clone', '-b', 'main', str(self.remote), str(self.editor))
        self.git('config', 'user.name', 'Editor', cwd=self.editor)
        self.git('config', 'user.email', 'editor@example.invalid', cwd=self.editor)
        self.initial = self.git('rev-parse', 'HEAD', cwd=self.repository)
        self.stage = self.root / 'stage'
        self.write(self.stage, 'notice/zh-Hans.json', '{"notice":"new translation"}\n')

    def git(self, *args, cwd=None):
        return self.execute(['git', *args], cwd=cwd, check=True, text=True,
                            capture_output=True).stdout.strip()

    def write(self, root, relative, text):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def push_edit(self, relative, text):
        self.write(self.editor, relative, text)
        self.git('add', '.', cwd=self.editor)
        self.git('commit', '-m', 'Collaboration update', cwd=self.editor)
        self.git('push', 'origin', 'main', cwd=self.editor)
        return self.git('rev-parse', 'HEAD', cwd=self.editor)

    def test_no_changed_exports_still_detects_new_human_data(self):
        self.push_edit('records/adv_test.json', '{}')
        with self.assertRaises(exports.TranslationBranchAdvanced):
            exports.sync_git(self.repository, [], self.initial)

    def test_isolated_commit_keeps_input_checkout_clean_and_unchanged(self):
        result = exports.sync_git(self.repository, ['notice/zh-Hans.json'], self.initial,
                                  source_dir=self.stage)
        self.assertEqual(self.git('rev-parse', 'HEAD', cwd=self.repository), self.initial)
        self.assertEqual(self.git('status', '--porcelain', cwd=self.repository), '')
        self.assertEqual((self.repository / 'notice/zh-Hans.json').read_text(), '{}\n')
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-parse', 'main'), result)
        worktrees = self.git('worktree', 'list', '--porcelain', cwd=self.repository)
        self.assertEqual(worktrees.count('worktree '), 1)

    def test_push_race_rebuilds_reviewed_story_and_master_title_before_commit(self):
        stem = 'adv_main_01_01_01'
        relative = 'story/reviewed/main/' + stem + '.csv'
        source = self.root / 'source'; source.mkdir()
        (source / (stem + '.txt')).write_text('original script')
        rows = [['0:title:0', '', '1話 始まり', ''],
                ['info', stem + '.txt', hashlib.sha256(b'original script').hexdigest(), ''],
                ['译者', '', '', '']]
        original_csv = self.root / 'csv'; original_csv.mkdir()
        with (original_csv / (stem + '.csv')).open('w', newline='') as stream:
            writer = csv.writer(stream); writer.writerow(imports.COLUMNS); writer.writerows(rows)
        record = {'schema_version': 1, 'file_id': stem, 'proofread': {'state': '完成', 'revision': 1},
                  'artifacts': {'proofread_csv': {'path': relative}}}
        self.write(self.editor, 'records/' + stem + '.json', json.dumps(record))
        csv_text = (original_csv / (stem + '.csv')).read_text().replace('1話 始まり,', '1話 始まり,1话 旧标题')
        initial_human = self.push_edit(relative, csv_text)
        project = self.root / 'project'
        self.write(project, 'translations/story-metadata/catalog.json',
                   json.dumps({'stories': {'story-1': {'assets': [stem]}}}))
        self.write(project, 'translations/master/orig/Story.json', json.dumps({'story-1': {'name': '始まり'}}))
        attempts = []
        injected = False
        def intercept(command, **kwargs):
            nonlocal injected
            if 'push' in command and not injected:
                injected = True
                self.push_edit(relative, csv_text.replace('旧标题', '最新校对'))
            self.assertNotIn('--force', command if 'push' in command else [])
            return self.execute(command, **kwargs)
        def finalize(revision):
            self.write(project, 'translations/master/zh-Hans/Story.json',
                       json.dumps({'story-1': {'name': '机器标题'}}))
            imports.import_stories(self.repository, original_csv, source, self.root / 'runtime',
                                   project, human_only=True)
            title = json.loads((project / 'translations/master/zh-Hans/Story.json').read_text())['story-1']['name']
            attempts.append((revision, title))
            return exports.sync_git(self.repository, ['notice/zh-Hans.json'], revision, source_dir=self.stage)
        with patch.object(exports.subprocess, 'run', side_effect=intercept):
            result = release.retry_collaboration_release(self.repository, self.initial, 'main', finalize)
        self.assertEqual([title for _, title in attempts], ['旧标题', '最新校对'])
        self.assertEqual(attempts[0][0], initial_human)
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-parse', 'main'), result)
        self.assertIn('最新校对', self.git('--git-dir', str(self.remote), 'show', 'main:' + relative))
        self.assertEqual(self.git('status', '--porcelain', cwd=self.repository), '')
        self.assertEqual(self.git('worktree', 'list', '--porcelain', cwd=self.repository).count('worktree '), 1)

    def test_conflicting_machine_inputs_stop_before_checkout_or_overwrite(self):
        self.push_edit('notice/zh-Hans.json', '{"notice":"human correction"}')
        with self.assertRaisesRegex(ValueError, 'Machine/source inputs changed'):
            exports.refresh_collaboration_checkout(self.repository, self.initial)
        self.assertEqual(self.git('rev-parse', 'HEAD', cwd=self.repository), self.initial)
        self.assertEqual((self.repository / 'notice/zh-Hans.json').read_text(), '{}\n')

    def test_dirty_input_checkout_is_never_reset(self):
        self.write(self.repository, 'README.md', 'Local edit')
        with self.assertRaisesRegex(ValueError, 'unexpected local changes'):
            exports.refresh_collaboration_checkout(self.repository, self.initial)
        self.assertEqual((self.repository / 'README.md').read_text(), 'Local edit')

    def test_unrelated_push_errors_are_not_retried_or_hidden(self):
        def fail_push(command, **kwargs):
            if 'push' in command:
                raise subprocess.CalledProcessError(128, command, stderr='permission denied')
            return self.execute(command, **kwargs)
        with patch.object(exports.subprocess, 'run', side_effect=fail_push):
            with self.assertRaises(subprocess.CalledProcessError):
                exports.sync_git(self.repository, ['notice/zh-Hans.json'], self.initial, source_dir=self.stage)
        self.assertEqual(self.git('status', '--porcelain', cwd=self.repository), '')
        self.assertEqual(self.git('--git-dir', str(self.remote), 'rev-parse', 'main'), self.initial)

    def test_continuous_edits_are_bounded_and_validation_errors_not_retried(self):
        with patch.object(exports, 'refresh_collaboration_checkout', return_value=self.initial), \
             patch('builtins.print'), \
             patch.object(exports, 'sync_git', side_effect=exports.TranslationBranchAdvanced('race')) as finalize:
            with self.assertRaisesRegex(exports.TranslationBranchAdvanced, '3 build attempts'):
                release.retry_collaboration_release(self.repository, self.initial, 'main', finalize)
            self.assertEqual(finalize.call_count, 3)
        with patch.object(exports, 'refresh_collaboration_checkout', return_value=self.initial), \
             patch.object(exports, 'sync_git', side_effect=ValueError('invalid translation')) as finalize:
            with self.assertRaisesRegex(ValueError, 'invalid translation'):
                release.retry_collaboration_release(self.repository, self.initial, 'main', finalize)
            finalize.assert_called_once()


if __name__ == '__main__':
    unittest.main()
