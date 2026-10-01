import json
from pathlib import Path
import tempfile
import unittest

from inspect_layout_capture import inspect


class InspectLayoutCaptureTests(unittest.TestCase):
    def test_outputs_only_selected_controls_and_preserves_changed_geometry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.layout.jsonl'
            row = dict(kind='layout', text='动作', object_path='Preview/Action/Label',
                       font_size=32, alignment=257, rect=[0, 0, 120, 40], anchored_position=[0, 0])
            records = [row, row, {**row, 'anchored_position': [3, 0]},
                       {**row, 'text': 'Private player value'}]
            path.write_text(''.join(json.dumps(value) + '\n' for value in records))
            result = inspect(path, {'动作'})
            self.assertEqual(len(result), 2)
            self.assertEqual(result[0]['observations'], 2)
            self.assertNotIn('Private player value', str(result))
            records[0]['rect'] = [0, 0, float('nan'), 40]
            path.write_text(json.dumps(records[0]) + '\n')
            with self.assertRaisesRegex(ValueError, 'invalid rect'):
                inspect(path, {'动作'})
