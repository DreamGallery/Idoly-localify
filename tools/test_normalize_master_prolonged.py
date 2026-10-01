import unittest

from normalize_master_prolonged import normalize


class NormalizeMasterProlongedTests(unittest.TestCase):
    def test_preserves_story_title_line_count(self):
        before = "那只兔子\n先生ー\nーーー！！"
        after = normalize("Story", "st-card-cca-05-goch-00-03", "name", before)
        self.assertEqual(after.count("\n"), before.count("\n"))
        self.assertNotIn("ー", after)

    def test_preserves_katakana_names(self):
        self.assertEqual(normalize("Skill", "1", "name", "ヨーソロー！"),
                         "ヨーソロー！")


if __name__ == "__main__":
    unittest.main()
