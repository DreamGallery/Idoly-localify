import json
from pathlib import Path
import tempfile
import unittest

from story_event_catalog import build

ROOT = Path(__file__).resolve().parents[1]


class EventCatalogTests(unittest.TestCase):
    def build_event_fixture(self, root, asset, reference_category="event"):
        rows = {
            "st-eve-2610-dice-001": {"categories": ["event"], "name": "Event story",
                "adv_play_types": [1], "assets": [asset],
                "references": [{"category": reference_category,
                                "event_id": "st-eve-2610-dice", "episode": 1}]},
            "st-eve-supported-001": {"categories": ["event"], "name": "Supported story",
                "adv_play_types": [1], "assets": ["event_2610_01_01"],
                "references": [{"category": "event", "event_id": "st-eve-supported",
                                "episode": 1}]},
        }
        for name, value in {"translations/story-metadata/catalog.json": {"stories": rows},
                            "translations/master/zh-Hans/Story.json": {}}.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value), encoding="utf-8")
        return json.loads(build(root, "event"))["stories"]

    def test_event_reusing_main_script_is_omitted_from_picker(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = self.build_event_fixture(root, "main_04_15_01")
            self.assertEqual([row["file"] for row in rows], ["adv_event_2610_01_01.txt"])
            # Selection must not modify the full category index used by translation.
            source = json.loads((root / "translations/story-metadata/catalog.json").read_text())
            self.assertEqual(source["stories"]["st-eve-2610-dice-001"]["assets"], ["main_04_15_01"])

    def test_short_script_is_omitted_from_picker(self):
        with tempfile.TemporaryDirectory() as directory:
            rows = self.build_event_fixture(Path(directory), "event_2610_01_01_short")
            self.assertEqual([row["file"] for row in rows], ["adv_event_2610_01_01.txt"])

    def test_invalid_script_still_blocks_catalog(self):
        for asset in ("../main_04_15_01", "main_04/15_01", "event_2610_01_01.txt", "", None):
            with self.subTest(asset=asset), tempfile.TemporaryDirectory() as directory:
                with self.assertRaisesRegex(ValueError, "Invalid story script"):
                    self.build_event_fixture(Path(directory), asset)

    def test_mismatched_reference_still_blocks_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Invalid story script"):
                self.build_event_fixture(Path(directory), "event_2610_01_01", "love")

    def test_bundled_catalogs_match_sources_and_scope(self):
        source = json.loads((ROOT / "translations/story-metadata/catalog.json").read_text())["stories"]
        for kind in ("event", "love"):
            actual = (ROOT / f"app/src/main/assets/idoly-localify/{kind}-stories.json").read_bytes()
            self.assertEqual(actual, build(ROOT, kind))
            rows = json.loads(actual)["stories"]
            self.assertEqual(len(rows), len({r["file"] for r in rows}))
            self.assertTrue(rows)
            for row in rows:
                self.assertTrue(row["file"].startswith(f"adv_{kind}_"))
                self.assertNotIn(f"第{row['episode']}话 · 第{row['episode']}话", row["display_label"])
                for story_id in row["story_ids"]:
                    self.assertEqual(source[story_id]["assets"], [row["file"][4:-4]])
                    self.assertEqual(source[story_id]["adv_play_types"], [1])
            if kind == "love":
                self.assertTrue(any(len(row["story_ids"]) > 1 and "共用剧情" in row["display_label"] for row in rows))
                self.assertTrue(any("幸福结局" in row["display_label"] for row in rows))
                early = next(row for row in rows if row["group_id"] == "love-23-0514")
                self.assertIn("一之濑怜篇", early["group_name"])
                self.assertEqual(early["display_label"], "第1话")

    def test_love_presentation_asset_is_not_the_script(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {"translations/story-metadata/catalog.json": {"stories": {
                "st-love-23-0514-001": {"categories": ["love"], "name": "第1話",
                    "adv_play_types": [1], "assets": ["love_2305_01_01"],
                    "references": [{"category": "love", "love_id": "love-23-0514", "episode": 1,
                                    "asset_id": "love-story-23-0514-001"}]},
                "multi": {"categories": ["event"], "adv_play_types": [1, 2],
                          "assets": ["event_01", "adv-live-01"]}}},
                "translations/master/zh-Hans/Story.json": {},
                "translations/story-metadata/love-group-names.json": {"groups": {
                    "love-23-0514": {"asset_id": "love-23-0514", "emblem_id": "actual-emblem",
                                      "original_name": "Original"}}},
                "translations/master/zh-Hans/Emblem.json": {"actual-emblem": {"name": "「正式活动名 · 角色篇」"}}}
            for name, value in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value))
            love_row = json.loads(build(root, "love"))["stories"][0]
            self.assertEqual(love_row["file"], "adv_love_2305_01_01.txt")
            self.assertEqual(love_row["display_label"], "第1话")
            self.assertEqual(love_row["group_name"], "正式活动名 · 角色篇")
            (root / "translations/story-metadata/love-group-names.json").unlink()
            self.assertEqual(json.loads(build(root, "love"))["stories"][0]["group_name"], "特殊活动 · 23-0514")
            self.assertEqual(json.loads(build(root, "event"))["stories"], [])


if __name__ == "__main__":
    unittest.main()
