import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from test_import_collaboration import CollaborationImportTests
import export_translation_sources as exports

class SourceExportTests(CollaborationImportTests):
    def test_plan_does_not_modify_public_data(self):
        paths = exports.export_sources(self.project, self.csv, self.sources, self.repo)
        self.assertIn('master/orig/Story.json', paths)
        self.assertFalse((self.repo / 'master').exists())
    def test_export_is_idempotent_and_preserves_human_files(self):
        human = self.layer('reviewed', '1话 人工')
        original = human.read_bytes()
        self.assertTrue(exports.export_sources(self.project, self.csv, self.sources, self.repo, apply=True))
        self.assertFalse(exports.export_sources(self.project, self.csv, self.sources, self.repo, apply=True))
        self.assertEqual(human.read_bytes(), original)
    def test_skip_human_story_does_not_overwrite_ai(self):
        ai = self.layer('ai', '1话 初译')
        original = ai.read_bytes()
        paths = exports.export_sources(self.project, self.csv, self.sources, self.repo, [self.stem], apply=True)
        self.assertFalse(any(path.startswith('story/ai') for path in paths))
        self.assertEqual(ai.read_bytes(), original)
    def test_stale_export_is_rejected(self):
        (self.sources / (self.stem + '.txt')).write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Stale story'):
            exports.export_sources(self.project, self.csv, self.sources, self.repo)
    def test_git_concurrent_update_never_pushes(self):
        from subprocess import CompletedProcess
        responses = [CompletedProcess([], 0, 'a'*40, ''), CompletedProcess([], 0, '', ''), CompletedProcess([], 0, 'b'*40, '')]
        with patch.object(exports.subprocess, 'run', side_effect=responses) as run:
            with self.assertRaisesRegex(ValueError, 'advanced'):
                exports.sync_git(self.repo, ['master/orig/Story.json'], 'a'*40)
        self.assertFalse(any('push' in c.args[0] for c in run.call_args_list))

    def test_metadata_only_csv_is_not_published(self):
        self.write_csv(self.csv / (self.stem + '.csv'), self.rows[1:])
        paths = exports.export_sources(self.project, self.csv, self.sources, self.repo)
        self.assertFalse(any(path.startswith('story/ai') for path in paths))
    def test_diagnostics_excluded_but_glossary_and_overrides_included(self):
        for relative in ('legal/runtime.json', 'legal/validation.json', 'ui/test-evidence.json',
                         'glossary/names.json', 'master/overrides.json',
                         'master/record-keys.json', 'master/field-keys.json'):
            self.json('translations/' + relative, {})
        paths = exports.export_sources(self.project, self.csv, self.sources, self.repo)
        self.assertIn('glossary/names.json', paths)
        self.assertIn('master/overrides.json', paths)
        self.assertIn('master/record-keys.json', paths)
        self.assertIn('master/field-keys.json', paths)
        self.assertNotIn('legal/runtime.json', paths)
        self.assertNotIn('legal/validation.json', paths)
        self.assertNotIn('ui/test-evidence.json', paths)
    def test_upstream_versions_and_hashes_reflect_current_snapshot(self):
        self.repo.mkdir()
        (self.repo / 'upstream.json').write_text(json.dumps({'story': {'resource_revision': 1055}, 'master': {'master_tag_version': 'old'}}))
        exports.export_sources(self.project, self.csv, self.sources, self.repo,
                               summary={'octo': {'after': 1066}, 'master_version': 'new'}, apply=True)
        value = json.loads((self.repo / 'upstream.json').read_text())
        self.assertEqual(value['story']['resource_revision'], 1066)
        self.assertEqual(value['master']['master_tag_version'], 'new')
        self.assertEqual(value['story']['translated_csv_files'], 1)
        self.assertEqual(value['story']['text_fields'], 1)
        self.assertEqual(len(value['story']['resource_snapshot_sha256']), 64)


class SourceGitTests(unittest.TestCase):
    def test_unicode_rename_and_large_batch_with_local_bare_remote(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); repository = root / 'checkout'; remote = root / 'remote.git'
            def git(*args, cwd=None):
                return subprocess.run(['git', *args], cwd=cwd, text=True,
                                      capture_output=True, check=True).stdout.strip()
            git('init', '--bare', str(remote))
            git('init', '-b', 'main', str(repository))
            git('config', 'user.name', 'Test', cwd=repository)
            git('config', 'user.email', 'test@example.invalid', cwd=repository)
            old = 'story/human/group/サニーピース/adv_group_sun_01_01.csv'
            new = 'story/drafts/translation/group/月のテンペスト/adv_group_sun_01_01.csv'
            (repository / old).parent.mkdir(parents=True)
            (repository / old).write_text('same contents for rename detection\n')
            git('add', '--', old, cwd=repository)
            git('commit', '-m', 'initial', cwd=repository)
            git('remote', 'add', 'origin', str(remote), cwd=repository)
            git('push', 'origin', 'main', cwd=repository)
            initial = git('rev-parse', 'HEAD', cwd=repository)
            (repository / new).parent.mkdir(parents=True)
            (repository / old).rename(repository / new)
            added = []
            for number in range(205):
                path = f'story/ai/adv_batch_{number}.csv'
                (repository / path).parent.mkdir(parents=True, exist_ok=True)
                (repository / path).write_text(f'new {number}')
                added.append(path)
            execute = subprocess.run
            commands = []
            def spy(command, **kwargs):
                commands.append(command)
                return execute(command, **kwargs)
            with patch.object(exports.subprocess, 'run', side_effect=spy):
                result = exports.sync_git(repository, [old, new, *added], initial,
                                          migration_paths=[old, new])
            self.assertNotEqual(result, initial)
            self.assertEqual(git('--git-dir', str(remote), 'rev-parse', 'refs/heads/main'), result)
            self.assertEqual(git('status', '--porcelain', cwd=repository), '')
            batches = [c[c.index('--') + 1:] for c in commands if 'add' in c]
            self.assertEqual([len(b) for b in batches], [100, 100, 7])
            self.assertTrue(any('--no-renames' in c for c in commands))
            self.assertTrue(all('core.quotepath=false' in c for c in commands))
