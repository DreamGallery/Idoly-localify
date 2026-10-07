"""Exercise real non-fast-forward races without network or force pushes."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import export_translation_sources as exports


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



if __name__ == '__main__':
    unittest.main()
