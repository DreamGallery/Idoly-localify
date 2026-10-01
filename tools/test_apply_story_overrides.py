import csv
import json
from pathlib import Path
import tempfile
import unittest

from apply_story_overrides import ROOT, plan


class StoryOverridesTests(unittest.TestCase):
    def setUp(self):
        self.override = json.loads((ROOT / "translations/story-metadata/overrides.json").read_text(encoding="utf-8"))[0]
        self.source_dir = ROOT.parent / "HoshimiToolkit/cache/resource/adventure"

    def test_current_csv_is_idempotent(self):
        _, changes = plan([self.override], ROOT.parent / "HoshimiToolkit/cache/csv", self.source_dir)
        self.assertEqual(changes[self.override["file"]], [])

    def test_rejects_unreviewed_translation_edit(self):
        with tempfile.TemporaryDirectory() as directory:
            csv_path = Path(directory) / self.override["file"]
            source = ROOT.parent / "HoshimiToolkit/cache/csv" / self.override["file"]
            with source.open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            next(row for row in rows if row["id"] == self.override["id"])["trans"] = "新的人工译文"
            with csv_path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"])
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "Translation edited since review"):
                plan([self.override], Path(directory), self.source_dir)


if __name__ == "__main__":
    unittest.main()
