import json
from pathlib import Path
import tempfile
import unittest

from master_json import read_table
from normalize_master_title_refs import apply, plan


class NormalizeMasterTitleRefsTests(unittest.TestCase):
    def test_aligns_only_exact_quoted_titles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            for folder, data in ((orig, {
                    "Card": {"card-1": {"name": "星をつかむ"}},
                    "Gacha": {"gacha-1": {"name": "君との夏"}},
                    "EventStory": {"event-1": {"description": "雪のフェス"}},
                    "Music": {"music-1": {"name": "秘密の歌"}},
                    "HomeTalk": {"88": {"displayConditionDescription":
                                           "星をつかむ fran\nストーリー3話を視聴"}},
                    "Mission": {"mission-1": {"name": "「雪のフェス」を視聴"}},
                    "Message": {"message-1": {"text": "『秘密の歌』を聞いた"}},
                    "HomeAction": {"action-1": {"text": "『秘密の歌』を歌う"}},
                    "Item": {"item-1": {"name": "「君との夏」専用券"},
                             "item-2": {"name": "「星をつかむ」記念"},
                             "item-3": {"name": "「別の夏」専用券"},
                             "item-4": {"name": "「君との夏」と「星をつかむ」"},
                             "item-5": {"name": "「雪のフェス」専用券"}}}),
                    (zh, {
                    "Card": {"card-1": {"name": "抓住星星"}},
                    "Gacha": {"gacha-1": {"name": "与你共度的夏天"}},
                    "EventStory": {"event-1": {"description": "雪之庆典"}},
                    "Music": {"music-1": {"name": "秘密之歌"}},
                    "HomeTalk": {"88": {"displayConditionDescription":
                                           "星をつかむ fran\n观看剧情第3话"}},
                    "Mission": {"mission-1": {"name": "观看「雪のフェス」"}},
                    "Message": {"message-1": {"text": "听了『秘密の歌』"}},
                    "HomeAction": {"action-1": {"text": "唱《秘密の歌》"}},
                    "Item": {"item-1": {"name": "「君との夏」专用券"},
                             "item-2": {"name": "「抓住那颗星」纪念"},
                             "item-3": {"name": "「别的夏天」专用券"},
                             "item-4": {"name": "「君との夏」和「星をつかむ」"},
                             "item-5": {"name": "《雪のフェス》专用券"}}})):
                for name, rows in data.items():
                    (folder / f"{name}.json").write_text(
                        json.dumps(rows, ensure_ascii=False), encoding="utf-8")
            changes = plan(orig, zh)
            self.assertEqual(len(changes), 7)
            self.assertEqual({item["id"] for item in changes},
                             {"88", "item-1", "item-2", "item-5", "mission-1",
                              "message-1", "action-1"})
            self.assertEqual({item["id"] for item in plan(orig, zh, True)},
                             {"88", "item-1", "item-5", "mission-1", "message-1",
                              "action-1"})
            self.assertEqual(apply(zh, changes), 7)
            table = read_table(zh / "Item.json")
            self.assertEqual(table["item-1"]["name"], "「与你共度的夏天」专用券")
            self.assertEqual(table["item-2"]["name"], "「抓住星星」纪念")
            self.assertEqual(read_table(zh / "HomeTalk.json")["88"][
                "displayConditionDescription"], "抓住星星 fran\n观看剧情第3话")
            self.assertEqual(read_table(zh / "Mission.json")["mission-1"]["name"],
                             "观看「雪之庆典」")
            self.assertEqual(read_table(zh / "Message.json")["message-1"]["text"],
                             "听了『秘密之歌』")
            self.assertEqual(read_table(zh / "HomeAction.json")["action-1"]["text"],
                             "唱《秘密之歌》")
            self.assertEqual(read_table(zh / "Item.json")["item-5"]["name"],
                             "《雪之庆典》专用券")


if __name__ == "__main__":
    unittest.main()
