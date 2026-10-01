import json
from pathlib import Path
import tempfile
import unittest

from fill_singers import compose, fill
from master_json import read_table


class FillSingersTests(unittest.TestCase):
    def test_compose_group_and_person_names(self):
        self.assertEqual(
            compose("月のテンペスト with 初音ミク",
                    {"初音ミク": "初音未来"},
                    {"月のテンペスト": "月光风暴"}),
            "月光风暴 with 初音未来")

    def test_fill_preserves_review_and_reports_unresolved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Music.json").write_text(json.dumps({
                "1": {"singer": "月のテンペスト with 初音ミク"},
                "2": {"singer": "月のテンペスト"},
                "3": {"singer": "未知の歌手"}}, ensure_ascii=False), encoding="utf-8")
            (zh / "Music.json").write_text(json.dumps({
                "2": {"singer": "已审校译名"}}, ensure_ascii=False), encoding="utf-8")
            filled, kept, unresolved = fill(orig, zh,
                                            {"初音ミク": "初音未来"},
                                            {"月のテンペスト": "月光风暴"})
            self.assertEqual((filled, kept), (1, 1))
            self.assertEqual(unresolved, {"未知の歌手": "未知の歌手"})
            result = read_table(zh / "Music.json")
            self.assertEqual(result["1"]["singer"], "月光风暴 with 初音未来")
            self.assertEqual(result["2"]["singer"], "已审校译名")
            self.assertNotIn("3", result)


if __name__ == "__main__":
    unittest.main()
