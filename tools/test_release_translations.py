import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import release_translations as release

class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.asset = self.root / 'text-update.zip'; self.asset.write_bytes(b'complete archive')
    def test_dry_run_never_uses_network(self):
        with patch.object(release, 'run') as run, patch.object(release, 'release_exists') as exists:
            result = release.publish('owner/repo', 'text-a', [self.asset], True, True)
        self.assertFalse(result['published']); run.assert_not_called(); exists.assert_not_called()
    def test_upload_failure_never_publishes_draft(self):
        commands = []
        def command(args):
            commands.append(args)
            if args[:3] == ['gh', 'release', 'upload']:
                raise subprocess.CalledProcessError(1, args)
        with patch.object(release, 'release_exists', return_value=None), patch.object(release, 'run', side_effect=command):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish('owner/repo', 'text-a', [self.asset], True, False)
        self.assertFalse(any('edit' in args for args in commands))
    def test_all_assets_uploaded_before_latest_is_published(self):
        manifest = self.root / 'manifest.json'; manifest.write_text('{}')
        with patch.object(release, 'release_exists', return_value=None), patch.object(release, 'run') as run:
            release.publish('owner/repo', 'text-a', [self.asset, manifest], True, False)
        args = [c.args[0] for c in run.call_args_list]
        self.assertEqual([a[2] for a in args], ['create', 'upload', 'upload', 'edit'])
        self.assertIn('--latest=true', args[-1])
    def test_apk_release_never_becomes_latest(self):
        with patch.object(release, 'release_exists', return_value=None), patch.object(release, 'run') as run:
            release.publish('owner/repo', 'v0.2.0', [self.asset], False, False)
        self.assertIn('--latest=false', run.call_args_list[-1].args[0])
    def test_existing_unchanged_assets_are_not_uploaded(self):
        def download(args):
            if 'download' not in args: return
            target = Path(args[args.index('--dir') + 1]) / self.asset.name
            target.write_bytes(self.asset.read_bytes())
        with patch.object(release, 'release_exists', return_value={'id': 1, 'assets': [{'name': 'text-update.zip'}]}), patch.object(release, 'run', side_effect=download) as run:
            result = release.publish('owner/repo', 'text-a', [self.asset], True, False)
        self.assertTrue(result['unchanged']); self.assertEqual(run.call_count, 2)
        self.assertIn('--latest=true', run.call_args_list[-1].args[0])
    def test_tag_collision_blocks_changed_asset(self):
        def download(args):
            (Path(args[args.index('--dir') + 1]) / self.asset.name).write_bytes(b'other content')
        with patch.object(release, 'release_exists', return_value={'id': 1, 'assets': [{'name': 'text-update.zip'}]}), patch.object(release, 'run', side_effect=download):
            with self.assertRaisesRegex(ValueError, 'differs'):
                release.publish('owner/repo', 'text-a', [self.asset], True, False)
    def test_auth_failure_is_not_treated_as_missing_release(self):
        with patch.object(release.subprocess, 'run', return_value=subprocess.CompletedProcess([], 1, '', 'HTTP 403')):
            with self.assertRaisesRegex(RuntimeError, 'GitHub access'):
                release.release_exists('owner/repo', 'text-a')
    def test_draft_resumes_only_missing_assets(self):
        manifest = self.root / 'manifest.json'; manifest.write_text('{}')
        def command(args):
            if 'download' in args:
                (Path(args[args.index('--dir') + 1]) / self.asset.name).write_bytes(self.asset.read_bytes())
        existing = {'draft': True, 'assets': [{'name': self.asset.name}]}
        with patch.object(release, 'release_exists', return_value=existing), patch.object(release, 'run', side_effect=command) as run:
            release.publish('owner/repo', 'text-a', [self.asset, manifest], True, False)
        args = [c.args[0] for c in run.call_args_list]
        self.assertEqual([a[2] for a in args], ['download', 'upload', 'edit'])
        self.assertEqual(args[1][4], manifest)
    def test_published_version_without_apk_fails(self):
        with self.assertRaisesRegex(ValueError, 'no expected APK'):
            release.needs_apk({'draft': False, 'assets': []}, '0.2.0')
        self.assertFalse(release.needs_apk({'assets': [{'name': 'idoly-localify-0.2.0.apk'}]}, '0.2.0'))

    def test_tracked_gitkeep_does_not_prevent_persistent_cache(self):
        toolkit, work = self.root / 'toolkit', self.root / 'work'
        cache = toolkit / 'cache'; cache.mkdir(parents=True)
        (cache / '.gitkeep').write_text('')
        target = release.prepare_toolkit_cache(toolkit, work)
        self.assertTrue(target.is_symlink())
        (target / 'OctoManifest.json').write_text('{}')
        self.assertTrue((work / 'toolkit-cache/OctoManifest.json').is_file())
        self.assertEqual(release.prepare_toolkit_cache(toolkit, work), target)

    def test_previous_human_csv_is_cleared_before_machine_memory(self):
        from types import SimpleNamespace
        source, csv_dir = self.root / 'source', self.root / 'csv'
        source.mkdir(); csv_dir.mkdir()
        (source / 'adv_main_01.txt').write_text('official source')
        (csv_dir / 'adv_removed_human.csv').write_text('revoked human text')
        toolkit = SimpleNamespace(export_file=lambda source, target: target.write_text('original fields, blank translation'))
        release.prepare_story_csv(source, csv_dir, toolkit)
        self.assertFalse((csv_dir / 'adv_removed_human.csv').exists())
        self.assertEqual((csv_dir / 'adv_main_01.csv').read_text(), 'original fields, blank translation')


if __name__ == '__main__': unittest.main()
