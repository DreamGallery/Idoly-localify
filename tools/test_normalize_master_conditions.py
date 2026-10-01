import unittest

from normalize_master_conditions import normalize, referenced_record


class NormalizeMasterConditionsTests(unittest.TestCase):
    def test_costume_name_uses_record_id_and_exact_source_name(self):
        original = {"cos-a": {"name": "スイートレディラビット"}}
        translated = {"cos-a": {"name": "甜美淑女兔"}}
        self.assertEqual(normalize("cd-possession-cos-a", "スイートレディラビットを持っている",
                                   "拥有スイートレディラビット", "Costume", "cos-a",
                                   original, translated, {}), "拥有甜美淑女兔")
        self.assertEqual(normalize("cd-possession-cos-a", "別の衣装を持っている",
                                   "拥有スイートレディラビット", "Costume", "cos-a",
                                   original, translated, {}), "拥有スイートレディラビット")

    def test_song_and_card_conditions_use_canonical_names(self):
        song_orig = {"music-1": {"name": "星のように夜を照らせ"}}
        song_zh = {"music-1": {"name": "如星般照亮夜晚"}}
        self.assertEqual(normalize("cd-time-music-1-a", "開始：星のように夜を照らせ楽曲",
                                   "开始：星のように夜を照らせ乐曲", "Music", "music-1",
                                   song_orig, song_zh, {}), "开始：如星般照亮夜晚乐曲")
        card_orig = {"card-1": {"name": "誰にも負けないくらい好き"}}
        card_zh = {"card-1": {"name": "喜欢到不输给任何人"}}
        self.assertEqual(normalize("cd-possession-card-1-not", "誰にも負けないくらい好き 長瀬琴乃を持っていない",
                                   "谁にも负けないくらい好き", "Card", "card-1",
                                   card_orig, card_zh, {"長瀬琴乃": "长濑琴乃"}),
                         "未持有「喜欢到不输给任何人」长濑琴乃")

    def test_record_id_must_match_full_prefix(self):
        self.assertEqual(referenced_record("cd-possession-cos-ab-not", "Costume",
                                           ["cos-ab", "cos-a"]), "cos-ab")
        self.assertIsNone(referenced_record("cd-possession-cos-b-not", "Costume",
                                            ["cos-a"]))
        self.assertIsNone(referenced_record("cd-possession-cos-ab-not", "Costume",
                                            ["cos-a"]))


if __name__ == "__main__":
    unittest.main()
