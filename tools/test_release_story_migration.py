"""Integration with the reviewed public migration implementation, no network writes."""
import csv
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest

import release_translations as release
from export_translation_sources import export_sources, migration_data_path, export_data_path
from import_collaboration import import_stories


class ReleaseMigrationTests(unittest.TestCase):
    def setUp(self):
        scripts = [release.ROOT.parent / name / 'scripts/story_migration.py'
                   for name in ('Idoly-localify-translations', 'translations-data')]
        script = next((p for p in scripts if p.is_file()), None)
        if script is None:
            self.skipTest('Checkout trusted translations data repository beside the private project')
        self.tmp = tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / 'data'; (self.repo / 'scripts').mkdir(parents=True)
        shutil.copyfile(script, self.repo / 'scripts/story_migration.py')
        self.source = self.root / 'source'; self.source.mkdir()
        self.csv = self.root / 'csv'; self.csv.mkdir()
        self.work = self.root / 'work'; self.work.mkdir()
        self.stem = 'adv_bond_ai_01_01'
        self.relative = 'bond/ai/' + self.stem + '.csv'
        self.old = '[message name=A text=同じ]\n[message name=A text=旧文]\n'
        self.new = '[message name=A text=同じ]\n[message name=A text=新文]\n'
        (self.source / (self.stem + '.txt')).write_text(self.new)
        self.write_csv(self.csv / (self.stem + '.csv'), self.new, ['机器保留', '机器新译'])
        for layer in ('ai', 'human', 'reviewed'):
            self.write_csv(self.repo / 'story' / layer / self.relative, self.old, ['人工保留', '旧译'])
        record = {'schema_version': 1, 'file_id': self.stem, 'artifacts': {}}
        for track, layer in [('translation', 'human'), ('proofread', 'reviewed')]:
            record[track] = {'state': '完成', 'revision': 3, 'operator_github': 'alice'}
            record['artifacts'][track + '_csv'] = {'path': 'story/' + layer + '/' + self.relative}
        (self.repo / 'records').mkdir()
        (self.repo / 'records' / (self.stem + '.json')).write_text(json.dumps(record))
    def write_csv(self, path, raw, translations):
        path.parent.mkdir(parents=True, exist_ok=True)
        texts = ['同じ', '新文' if raw == self.new else '旧文']
        rows = [[f'{n}:text:1', 'A', text, trans] for n, (text, trans) in enumerate(zip(texts, translations), 1)]
        rows += [['info', self.stem + '.txt', hashlib.sha256(raw.encode()).hexdigest(), ''], ['译者', '', '', '']]
        with path.open('w', newline='') as stream:
            writer = csv.writer(stream); writer.writerow(['id', 'name', 'text', 'trans']); writer.writerows(rows)
    def snapshot(self):
        return {p.relative_to(self.repo).as_posix(): p.read_bytes() for p in self.repo.rglob('*') if p.is_file()}
    def test_official_change_migrates_human_before_import_but_preserves_new_ai(self):
        before = self.snapshot()
        stage, receipt, baseline = release.prepare_collaboration_stage(self.repo, self.source, self.csv, self.work)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(receipt['source_changed'], [self.stem])
        self.assertFalse((stage / 'story/human' / self.relative).exists())
        draft = (stage / 'story/drafts/translation' / self.relative).read_text()
        self.assertIn('人工保留', draft); self.assertNotIn('旧译', draft)
        record = json.loads((stage / 'records' / (self.stem + '.json')).read_text())
        self.assertEqual(record['translation']['state'], '进行中')
        self.assertEqual(record['translation']['operator_github'], 'alice')
        self.assertTrue(list((stage / 'archive/backups').rglob('*.csv')))
        import_stories(stage, self.csv, self.source, self.root / 'runtime', self.root, human_only=True, apply_human_titles=False)
        exported = export_sources(self.root, self.csv, self.source, stage, apply=True)
        paths = set(exported) | set(receipt['changed_paths'])
        dry = release.install_collaboration_stage(self.repo, stage, paths, receipt['changed_paths'], baseline)
        self.assertEqual(self.snapshot(), before)
        applied = release.install_collaboration_stage(self.repo, stage, paths, receipt['changed_paths'], baseline, apply=True)
        self.assertEqual(dry, applied)
        self.assertIn('机器新译', (self.repo / 'story/ai' / self.relative).read_text())
        self.assertFalse((self.repo / 'story/reviewed' / self.relative).exists())
    def test_concurrent_human_edit_blocks_before_writes(self):
        stage, receipt, baseline = release.prepare_collaboration_stage(self.repo, self.source, self.csv, self.work)
        path = self.repo / 'story/human' / self.relative
        path.write_text(path.read_text() + '\n')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, 'changed during release'):
            release.install_collaboration_stage(self.repo, stage, receipt['changed_paths'], receipt['changed_paths'], baseline, apply=True)
        self.assertEqual(self.snapshot(), before)
    def test_source_mismatch_keeps_checkout_untouched(self):
        before = self.snapshot()
        (self.source / (self.stem + '.txt')).write_text('different source')
        with self.assertRaises(ValueError):
            release.prepare_collaboration_stage(self.repo, self.source, self.csv, self.work)
        self.assertEqual(self.snapshot(), before)
    def test_refresh_does_not_execute_newly_fetched_migration_code(self):
        pinned = self.work / 'pinned-migration.py'
        shutil.copyfile(self.repo / 'scripts/story_migration.py', pinned)
        (self.repo / 'scripts/story_migration.py').write_text('raise RuntimeError("untrusted new code")')
        stage, receipt, _ = release.prepare_collaboration_stage(
            self.repo, self.source, self.csv, self.work, migration_script=pinned)
        self.assertEqual(receipt['source_changed'], [self.stem])
        self.assertIn('untrusted new code', (stage / 'scripts/story_migration.py').read_text())
    def test_strict_paths_do_not_allow_code_or_traversal(self):
        for path in ('scripts/evil.py', '.github/workflows/x.yml', 'records/../../evil.json',
                     'story/human/../../evil.csv', 'story/human//a.csv', 'archive/backups/bad/story/human/a.csv'):
            self.assertFalse(migration_data_path(path), path)
            self.assertFalse(export_data_path(path), path)
        self.assertTrue(migration_data_path('story/drafts/proofread/group/サニーピース/adv_test.csv'))
        with self.assertRaises(ValueError):
            release.install_collaboration_stage(self.repo, self.repo, ['scripts/evil.py'], ['scripts/evil.py'], {}, apply=True)

if __name__ == '__main__': unittest.main()
