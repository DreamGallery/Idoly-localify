import json
from pathlib import Path
import tempfile
import unittest

from audit_master_json import audit


class AuditMasterJsonTests(unittest.TestCase):
    def test_flags_japanese_prolonged_mark_in_chinese_line(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Message.json").write_text(json.dumps({
                "1": {"text": "誕生日おめでとうー！"},
                "2": {"text": "サマーホリデイ"}}, ensure_ascii=False), encoding="utf-8")
            (zh / "Message.json").write_text(json.dumps({
                "1": {"text": "生日快乐ー！"},
                "2": {"text": "サマーホリデイ"}}, ensure_ascii=False), encoding="utf-8")
            counts, issues = audit(orig, zh, {})
            self.assertEqual(counts["prolonged_mark"], 1)
            self.assertEqual(issues["prolonged_mark"][0]["id"], "1")
            self.assertEqual(counts["kana_remaining"], 1)

    def test_middle_dot_does_not_count_as_japanese_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Card.json").write_text(json.dumps({
                "1": {"name": "誕生日トラベル・愛"},
                "2": {"name": "特別な日"}}), encoding="utf-8")
            (zh / "Card.json").write_text(json.dumps({
                "1": {"name": "生日旅行・爱"},
                "2": {"name": "特别な日"}}), encoding="utf-8")
            counts, issues = audit(orig, zh, {})
            self.assertEqual(counts["kana_remaining"], 1)
            self.assertEqual(issues["kana"][0]["id"], "2")

    def test_normalizes_only_terms_present_in_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Story.json").write_text(json.dumps({
                "1": {"name": "ⅢXの物語", "description": "別の物語"}}), encoding="utf-8")
            (zh / "Story.json").write_text(json.dumps({
                "1": {"name": "ⅢX的故事", "description": "ⅢX的故事"}}), encoding="utf-8")
            counts, _ = audit(orig, zh, {"ⅢX": "IIIX"}, apply=True)
            self.assertEqual(counts["terms_normalized"], 1)
            data = json.loads((zh / "Story.json").read_text())
            self.assertEqual(data["1"]["name"], "IIIX的故事")
            self.assertEqual(data["1"]["description"], "ⅢX的故事")

    def test_normalizes_source_conditioned_machine_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Story.json").write_text(json.dumps({
                "1": {"name": "ぱじゃパ！結成"},
                "2": {"name": "別のグループ"}}, ensure_ascii=False), encoding="utf-8")
            (zh / "Story.json").write_text(json.dumps({
                "1": {"name": "睡衣啪！成立"},
                "2": {"name": "睡衣啪！成立"}}, ensure_ascii=False), encoding="utf-8")
            counts, _ = audit(orig, zh, {"ぱじゃパ！": "睡衣派对"},
                              apply=True, aliases={"ぱじゃパ！": ["睡衣啪！"]})
            self.assertEqual(counts["terms_normalized"], 1)
            data = json.loads((zh / "Story.json").read_text(encoding="utf-8"))
            self.assertEqual(data["1"]["name"], "睡衣派对成立")
            self.assertEqual(data["2"]["name"], "睡衣啪！成立")

    def test_retained_title_does_not_hide_other_japanese_text(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Music.json").write_text(json.dumps({
                "1": {"name": "ありがとう to You"},
                "2": {"description": "ありがとう to You の曲"}},
                ensure_ascii=False), encoding="utf-8")
            (zh / "Music.json").write_text(json.dumps({
                "1": {"name": "ありがとう to You"},
                "2": {"description": "ありがとう to You の歌曲"}},
                ensure_ascii=False), encoding="utf-8")
            counts, issues = audit(orig, zh, {}, retained_titles={
                "ありがとう to You": "https://idolypride.jp/"})
            self.assertEqual(counts["retained_kana"], 1)
            self.assertEqual(counts["kana_remaining"], 1)
            self.assertEqual(issues["kana"][0]["id"], "2")

    def test_original_music_credit_does_not_count_as_untranslated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            source = {"1": {"lyricist": "たなかまゆ",
                            "arranger": "岡田ピロー", "name": "日本語曲"}}
            target = {"1": {"lyricist": "たなかまゆ",
                            "arranger": "岡田ピロー", "name": "中文歌曲"}}
            (orig / "Music.json").write_text(json.dumps(source, ensure_ascii=False),
                                               encoding="utf-8")
            (zh / "Music.json").write_text(json.dumps(target, ensure_ascii=False),
                                             encoding="utf-8")
            counts, issues = audit(orig, zh, {})
            self.assertEqual(counts["retained_credit"], 2)
            self.assertNotIn("kana_remaining", counts)
            self.assertNotIn("unchanged", counts)
            self.assertEqual(issues["kana"], [])


if __name__ == "__main__":
    unittest.main()
