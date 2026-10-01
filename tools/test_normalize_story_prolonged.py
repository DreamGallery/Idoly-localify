import unittest

from normalize_story_prolonged import normalize


class NormalizeStoryProlongedTests(unittest.TestCase):
    def test_chinese_interjection_uses_chinese_punctuation(self):
        self.assertEqual(normalize("おはようございまーす！", "早上好ー！"), "早上好～！")
        self.assertEqual(normalize("おーーー！！", "哦ーーー！！"), "哦——！！")

    def test_preserves_names_and_unrelated_text(self):
        self.assertEqual(normalize("ヨーソロー！", "ヨーソロー！"), "ヨーソロー！")
        self.assertEqual(normalize("ありがとう", "谢谢ー！"), "谢谢ー！")


if __name__ == "__main__":
    unittest.main()
