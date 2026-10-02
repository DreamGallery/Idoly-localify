import csv
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

import import_collaboration as imports

class CollaborationImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / 'repo'; self.project = self.root / 'project'
        self.sources = self.root / 'source'; self.csv = self.root / 'csv'; self.out = self.root / 'out'
        self.sources.mkdir(); self.csv.mkdir()
        self.stem = 'adv_main_01_01_01'
        (self.sources / (self.stem + '.txt')).write_bytes(b'original script')
        self.rows = [{'id': '0:title:0', 'name': '', 'text': '1話 始まり', 'trans': ''},
                     {'id': 'info', 'name': self.stem + '.txt', 'text': hashlib.sha256(b'original script').hexdigest(), 'trans': ''},
                     {'id': '译者', 'name': '', 'text': '', 'trans': ''}]
        self.write_csv(self.csv / (self.stem + '.csv'), self.rows)
        self.json('translations/story-metadata/catalog.json', {'stories': {
            'story-1': {'assets': ['main_01_01_01']}, 'unrelated': {'assets': ['other']}}})
        self.json('translations/master/orig/Story.json', {'story-1': {'name': '始まり'}, 'unrelated': {'name': '始まり'}})
        self.json('translations/master/zh-Hans/Story.json', {'story-1': {'name': '旧译'}, 'unrelated': {'name': '不能改'}})
    def json(self, relative, value):
        path = self.project / relative; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False))
    def write_csv(self, path, rows):
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=imports.COLUMNS)
            writer.writeheader(); writer.writerows(rows)
    def layer(self, name, translation, state='完成'):
        rows = [dict(r) for r in self.rows]; rows[0]['trans'] = translation
        path = self.repo / 'story' / name / 'main' / (self.stem + '.csv')
        self.write_csv(path, rows)
        if name != 'ai':
            record = self.repo / 'records' / (self.stem + '.json'); record.parent.mkdir(exist_ok=True)
            value = json.loads(record.read_text()) if record.exists() else {'schema_version': 1, 'file_id': self.stem, 'artifacts': {}}
            track, artifact = ('proofread', 'proofread_csv') if name == 'reviewed' else ('translation', 'translation_csv')
            value[track] = {'state': state, 'revision': 1}
            value['artifacts'][artifact] = {'path': path.relative_to(self.repo).as_posix()}
            record.write_text(json.dumps(value))
        return path
    def run_import(self):
        return imports.import_stories(self.repo, self.csv, self.sources, self.out, self.project)
    def test_reviewed_wins_and_only_linked_master_title_changes(self):
        self.layer('ai', '1话 初译'); self.layer('human', '1话 人工'); self.layer('reviewed', '1话 校对')
        result = self.run_import()
        self.assertEqual(result['stories'][self.stem]['layer'], 'reviewed')
        data = json.loads((self.project / 'translations/master/zh-Hans/Story.json').read_text())
        self.assertEqual(data['story-1']['name'], '校对')
        self.assertEqual(data['unrelated']['name'], '不能改')
        self.assertIn('校对', (self.out / (self.stem + '.csv')).read_text())
    def test_stale_hash_blocks_import(self):
        path = self.layer('reviewed', '1话 校对')
        path.write_text(path.read_text().replace(self.rows[1]['text'], 'a' * 64))
        with self.assertRaisesRegex(ValueError, 'Stale'): self.run_import()

    def test_reviewed_title_overrides_master_with_display_line_breaks(self):
        self.rows[0]['text'] = '伊吹渚 1話 ちょっと顔熱いかも'
        self.write_csv(self.csv / (self.stem + '.csv'), self.rows)
        self.layer('reviewed', '伊吹渚 1话 脸好像有点烫')
        for original in ('ちょっと\\n顔熱いかも', 'ちょっと\r\n顔熱いかも'):
            self.json('translations/master/orig/Story.json', {'story-1': {'name': original},
                'unrelated': {'name': original}})
            result = self.run_import()
            self.assertEqual(result['master_title_overrides'][0]['translation'], '脸好像有点烫')
            data = json.loads((self.project / 'translations/master/zh-Hans/Story.json').read_text())
            self.assertEqual(data['story-1']['name'], '脸好像有点烫')
            self.assertEqual(data['unrelated']['name'], '不能改')
        # A real wording difference must not be normalized away.
        self.json('translations/master/orig/Story.json', {'story-1': {'name': '違うタイトル'}})
        self.assertEqual(self.run_import()['master_title_overrides'], [])
    def test_modified_original_field_blocks_import(self):
        path = self.layer('human', '1话 人工')
        path.write_text(path.read_text().replace('1話 始まり', '1話 変更'))
        with self.assertRaisesRegex(ValueError, 'changed original'): self.run_import()
    def test_incomplete_record_blocks_import(self):
        self.layer('human', '1话 人工', state='进行中')
        with self.assertRaisesRegex(ValueError, 'Unfinished'): self.run_import()
    def test_artifact_traversal_does_not_read_outside_repository(self):
        self.layer('reviewed', '1话 校对')
        path = self.repo / 'records' / (self.stem + '.json')
        value = json.loads(path.read_text()); value['artifacts']['proofread_csv']['path'] = '../../script.py'
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'mismatched'): self.run_import()
    def test_symlink_data_is_rejected(self):
        path = self.layer('ai', '1话 初译')
        (path.parent / 'evil.csv').symlink_to(path)
        with self.assertRaisesRegex(ValueError, 'Symlinked'): self.run_import()
    def test_machine_titles_do_not_override_master(self):
        self.layer('ai', '1话 初译')
        self.assertEqual(self.run_import()['master_title_overrides'], [])
    def test_human_only_preserves_new_machine_translation(self):
        self.rows[0]['trans'] = '1话 新的机器译文'
        self.write_csv(self.csv / (self.stem + '.csv'), self.rows)
        self.layer('ai', '1话 旧初译')
        imports.import_stories(self.repo, self.csv, self.sources, self.out, self.project, human_only=True)
        self.assertIn('新的机器译文', (self.out / (self.stem + '.csv')).read_text())
    def test_unreviewed_human_title_does_not_override_master(self):
        self.layer('human', '1话 人工未校对')
        self.assertEqual(self.run_import()['master_title_overrides'], [])

    def test_ai_stage_ignores_human_artifacts(self):
        self.layer('ai', '1话 机器'); self.layer('reviewed', '1话 人工')
        result = imports.import_stories(self.repo, self.csv, self.sources, self.out, self.project,
                                        ai_only=True, apply_human_titles=False)
        self.assertEqual(result['stories'][self.stem]['layer'], 'ai')
        self.assertIn('机器', (self.out / (self.stem + '.csv')).read_text())
        self.assertNotIn('人工', (self.out / (self.stem + '.csv')).read_text())

    def test_complete_record_with_empty_translation_fails(self):
        self.layer('reviewed', '')
        with self.assertRaisesRegex(ValueError, 'empty translations'):
            self.run_import()

    def test_final_coverage_is_checked_after_layering(self):
        self.run_import()
        with self.assertRaisesRegex(ValueError, 'Final story coverage incomplete'):
            imports.validate_story_coverage(self.out)

    def test_official_refresh_precedes_import_of_new_public_ai(self):
        import release_translations as release
        from unittest.mock import patch
        self.layer('ai', '1话 新剧情初译')
        (self.csv / (self.stem + '.csv')).unlink()  # Hoshimi-Adv CSV is older.
        phases = []
        def refresh(command, project):
            if '--reuse-fetched' not in command:
                phases.append('fetch')
                self.write_csv(self.csv / (self.stem + '.csv'), self.rows)
            else:
                phases.append('translate')
                self.assertIn('新剧情初译', (self.csv / (self.stem + '.csv')).read_text())
        with patch.object(release, 'run', side_effect=refresh):
            result = release.refresh_and_import_machine(self.repo, self.sources, self.csv, self.project, ['refresh'])
        self.assertEqual(phases, ['fetch', 'translate'])
        self.assertEqual(result['stories'][self.stem]['layer'], 'ai')


if __name__ == '__main__': unittest.main()
