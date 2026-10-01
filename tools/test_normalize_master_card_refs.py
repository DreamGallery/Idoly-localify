import json
from pathlib import Path
import tempfile
import unittest

from master_json import read_table
from normalize_master_card_refs import apply, plan


class NormalizeMasterCardRefsTests(unittest.TestCase):
    def test_uses_matching_card_id_and_source_title(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            for folder, data in ((orig, {
                    "Card": {"card-a-01": {"name": "星すくう指先"}},
                    "ShowcaseToy": {
                        "toy_card-a-01": {"name": "缶バッジ 琴乃「星すくう指先」"},
                        "toy_card-a-02": {"name": "缶バッジ 琴乃「星すくう指先」"},
                        "toy_card-b-01": {"name": "缶バッジ 琴乃「別の名前」"}}}),
                    (zh, {
                    "Card": {"card-a-01": {"name": "摘星的指尖"}},
                    "ShowcaseToy": {
                        "toy_card-a-01": {"name": "徽章 琴乃「摘星指尖」"},
                        "toy_card-a-02": {"name": "徽章 琴乃「摘星指尖」"},
                        "toy_card-b-01": {"name": "徽章 琴乃「另一个名字」"}}})):
                for name, rows in data.items():
                    (folder / f"{name}.json").write_text(
                        json.dumps(rows, ensure_ascii=False), encoding="utf-8")
            changes = plan(orig, zh)
            self.assertEqual(len(changes), 2)
            self.assertEqual(changes[0]["new"], "徽章 琴乃「摘星的指尖」")
            self.assertEqual(apply(zh, changes), 2)
            self.assertEqual(read_table(zh / "ShowcaseToy.json")["toy_card-a-01"]["name"],
                             "徽章 琴乃「摘星的指尖」")
            self.assertEqual(read_table(zh / "ShowcaseToy.json")["toy_card-a-02"]["name"],
                             "徽章 琴乃「摘星的指尖」")


if __name__ == "__main__":
    unittest.main()
