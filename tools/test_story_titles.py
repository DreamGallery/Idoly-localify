import csv
from pathlib import Path
import tempfile
import unittest

from build_story_titles import collect_titles


class StoryTitleTest(unittest.TestCase):
    def test_episode_suffix_and_conflicting_translations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "adv.csv"
            with path.open("w", encoding="utf-8", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=["id", "name", "text", "trans"])
                writer.writeheader()
                writer.writerow({"id": "1:title:1", "name": "",
                                 "text": "星見編1章2話 あれから三年",
                                 "trans": "星见篇第1章第2话 从那之后三年"})
                writer.writerow({"id": "2:title:1", "name": "",
                                 "text": "星見編1章3話 あれから三年",
                                 "trans": "星见篇第1章第3话 自那时起三年"})
            safe, conflicts = collect_titles(root)
            self.assertEqual(safe["星見編1章2話 あれから三年"],
                             "星见篇第1章第2话 从那之后三年")
            self.assertNotIn("あれから三年", safe)
            self.assertEqual(conflicts["あれから三年"],
                             ["从那之后三年", "自那时起三年"])


if __name__ == "__main__":
    unittest.main()
