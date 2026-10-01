import json
from pathlib import Path
import unittest
import tempfile

from story_card_catalog import build


ROOT = Path(__file__).resolve().parents[1]


class CardCatalogTests(unittest.TestCase):
    def test_bundled_card_catalog_matches_reviewed_sources(self):
        required = ("translations/story-metadata/catalog.json", "translations/master/zh-Hans/Story.json",
                    "translations/master/zh-Hans/Card.json", "translations/glossary/names.json")
        if not all((ROOT / name).is_file() for name in required):
            self.skipTest("Reviewed translation data is not included in the code checkout")
        actual = (ROOT / "app/src/main/assets/idoly-localify/card-stories.json").read_bytes()
        self.assertEqual(actual, build(ROOT))
        rows = json.loads(actual)["stories"]
        self.assertEqual(len(rows), len({row["file"] for row in rows}))
        self.assertTrue(all(row["file"].startswith("adv_card_") and
                            row["file"].endswith(".txt") for row in rows))
        self.assertTrue(all(row["episode"] > 0 for row in rows))

    def test_character_names_use_reviewed_dictionary_and_unknown_data_is_retained(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = {
                "translations/story-metadata/catalog.json": {"stories": {
                    "known": {"categories": ["card"], "name": "Known", "assets": ["card_other_01"],
                        "references": [{"category": "card", "card_id": "card-01", "episode": 1,
                            "character_id": "char-ai", "character_code": "ai",
                            "character_name": "小美山愛"}]},
                    "unnamed": {"categories": ["card"], "name": "Unknown", "assets": ["card_ai_02"],
                        "references": [{"category": "card", "card_id": "card-02", "episode": 1}]},
                    "untranslated": {"categories": ["card"], "name": "Guest", "assets": ["card_guest_01"],
                        "references": [{"category": "card", "card_id": "card-03", "episode": 1,
                            "character_id": "char-guest", "character_code": "guest",
                            "character_name": "Guest Original"}]}}},
                "translations/master/zh-Hans/Story.json": {},
                "translations/master/zh-Hans/Card.json": {"card-01": {}, "card-02": {}, "card-03": {}},
                "translations/master/orig/Card.json": {"card-01": {}, "card-02": {"name": "Official new card"}, "card-03": {}},
                "translations/glossary/names.json": {"小美山愛": "小美山爱"},
            }
            for name, value in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value), encoding="utf-8")
            known, unnamed, untranslated = json.loads(build(root))["stories"]
            self.assertEqual((known["character_id"], known["character_code"], known["character_name"]),
                             ("char-ai", "ai", "小美山爱"))
            self.assertEqual((unnamed["character_id"], unnamed["character_name"]), ("", ""))
            self.assertEqual(unnamed["card_name"], "Official new card")
            self.assertEqual(untranslated["character_name"], "Guest Original")
            card_path = root / "translations/master/zh-Hans/Card.json"
            card_path.write_text(json.dumps({"card-01": {}, "card-03": {}}), encoding="utf-8")
            self.assertEqual(json.loads(build(root))["stories"][1]["card_name"], "Official new card")
            source = root / "translations/master/orig/Card.json"
            source.write_text(json.dumps({"card-01": {}, "card-03": {}}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "invalid resource or card"):
                build(root)


if __name__ == "__main__":
    unittest.main()
