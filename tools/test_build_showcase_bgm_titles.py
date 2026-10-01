import json
from pathlib import Path
import tempfile
import unittest

from build_showcase_bgm_titles import build


class ShowcaseBgmTitleTests(unittest.TestCase):
    def test_catalog_join_deduplicates_and_excludes_non_showcase_music(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            showcase, music = root / 'ShowcaseMusic.json', root / 'Music.json'
            showcase.write_text(json.dumps([{'musicId': 'one'}, {'musicId': 'one'}]))
            music.write_text(json.dumps([
                {'id': 'one', 'name': 'MELODIES', 'description': 'MELODIES'},
                {'id': 'other', 'name': 'OTHER', 'description': 'OTHER'}]))
            output = build(showcase, music)
            self.assertEqual(output.count('U"MELODIES"'), 1)
            self.assertNotIn('OTHER', output)
            showcase.write_text(json.dumps([{'musicId': 'missing'}]))
            with self.assertRaises(KeyError):
                build(showcase, music)
            showcase.write_text('[]')
            with self.assertRaisesRegex(ValueError, 'Empty'):
                build(showcase, music)
            showcase.unlink()
            with self.assertRaises(FileNotFoundError):
                build(showcase, music)


if __name__ == '__main__':
    unittest.main()
