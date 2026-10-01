import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from check_capture_export import verify


class CaptureExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.status = self.root / 'capture.status.json'
        self.data = {'schema_version': 1, 'text_file': 'text-123.jsonl',
                     'stopped': True, 'complete': True,
                     'truncated': False, 'write_failed': False, 'file_size_mismatch': False,
                     'jsonl_may_be_incomplete': False,
                     'bytes_written': 0, 'dropped_records': 0}
        for kind in ('text', 'layout'):
            content = (json.dumps({'kind': kind}) + '\n').encode()
            name = f'capture (1).{kind}.jsonl'
            (self.root / name).write_bytes(content)
            self.data[f'exported_{kind}_file'] = name
            self.data[f'exported_{kind}_bytes'] = len(content)
            self.data[f'{kind}_sha256'] = hashlib.sha256(content).hexdigest()
            self.data[f'{kind}_records'] = 1
            self.data['bytes_written'] += len(content)

    def save(self):
        self.status.write_text(json.dumps(self.data))

    def test_complete_with_provider_renamed_files(self):
        self.save()
        result = verify(self.status)
        self.assertTrue(result['complete'])
        self.assertEqual(result['text_records'], 1)

    def test_partial_is_not_upgraded_even_with_valid_files(self):
        self.data.update(complete=False, truncated=True, dropped_records=3)
        self.save()
        self.assertEqual(verify(self.status)['state'], 'partial')

    def test_missing_legacy_status_is_unknown(self):
        self.assertEqual(verify(self.status)['state'], 'unknown')

    def test_tampering_and_wrong_record_counts_are_rejected(self):
        self.data['text_records'] = 2
        self.save()
        with self.assertRaises(ValueError):
            verify(self.status)
        self.data['text_records'] = 1
        self.save()
        original = self.root / self.data['exported_text_file']
        original.write_bytes(b'X' * original.stat().st_size)
        with self.assertRaises(ValueError):
            verify(self.status)
        self.data['text_sha256'] = hashlib.sha256(original.read_bytes()).hexdigest()
        self.save()
        with self.assertRaises(ValueError):
            verify(self.status)

    def test_path_escape_and_symlink_are_rejected(self):
        original_name = self.data['exported_text_file']
        self.data['exported_text_file'] = '../outside.jsonl'
        self.save()
        with self.assertRaises(ValueError):
            verify(self.status)
        self.data['exported_text_file'] = 'linked.jsonl'
        (self.root / 'linked.jsonl').symlink_to(self.root / original_name)
        self.save()
        with self.assertRaises(ValueError):
            verify(self.status)

    def test_partial_disk_write_can_preserve_invalid_last_line(self):
        path = self.root / self.data['exported_text_file']
        path.write_bytes(b'{"kind":')
        self.data.update(complete=False, write_failed=True, file_size_mismatch=True,
                         jsonl_may_be_incomplete=True, dropped_records=1)
        self.data['exported_text_bytes'] = path.stat().st_size
        self.data['text_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.save()
        result = verify(self.status)
        self.assertEqual(result['state'], 'partial')
        self.assertFalse(result['jsonl_valid'])

    def test_unicode_separators_inside_json_are_not_record_boundaries(self):
        path = self.root / self.data['exported_text_file']
        content = (json.dumps({'kind': 'text', 'source': '前\u2028中\u2029後\u0085終'},
                              ensure_ascii=False) + '\n').encode('utf-8')
        path.write_bytes(content)
        self.data['bytes_written'] += len(content) - self.data['exported_text_bytes']
        self.data['exported_text_bytes'] = len(content)
        self.data['text_sha256'] = hashlib.sha256(content).hexdigest()
        self.save()
        self.assertTrue(verify(self.status)['complete'])


if __name__ == '__main__':
    unittest.main()
