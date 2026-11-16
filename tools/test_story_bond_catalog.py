import json
from pathlib import Path
import tempfile
import unittest

from story_bond_catalog import build
from story_catalog_payloads import validate_catalog


class BondCatalogTests(unittest.TestCase):
    def test_character_relation_translation_and_legacy_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bond = {"categories": ["company"], "assets": ["bond_ai_01_02"],
                    "adv_play_types": [1], "name": "原題", "references": [{
                        "category": "company", "table": "Character", "character_id": "char-ai",
                        "episode": 2, "asset_id": "02"}]}
            catalog = {"bond": bond, "card": {"categories": ["card"], "references": [{
                "category": "card", "character_id": "char-ai", "character_name": "小美山愛",
                "character_code": "ai"}]}}
            files = {"story-metadata/catalog.json": {"stories": catalog},
                     "master/zh-Hans/Story.json": {"bond": {"name": "人工标题"}},
                     "glossary/names.json": {"小美山愛": "小美山爱"}}
            for name, value in files.items():
                path = root / "translations" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value))
            def current():
                (root / "translations/story-metadata/catalog.json").write_text(json.dumps({"stories": catalog}))
                return validate_catalog("bond-stories.json", build(root))["stories"]
            row, = current()
            self.assertEqual((row["group_id"], row["group_name"], row["episode"], row["title"]),
                             ("char-ai", "小美山爱", 2, "人工标题"))
            self.assertEqual(row["file"], "adv_bond_ai_01_02.txt")
            del catalog["card"]
            bond["references"][0].update(character_name="小美山愛", character_code="ai")
            self.assertEqual(current()[0], row)
            # A company reference to another script family or multi-asset playback
            # must not leak into the bond-only picker.
            for assets, play_types in [(["main_01_01"], [1]), (["bond_ai_01_02_short"], [1]),
                                       (["bond_ai_01_02", "bond_ai_01_03"], [1]),
                                       (["bond_ai_01_02"], [1, 2])]:
                bond.update(assets=assets, adv_play_types=play_types)
                self.assertEqual(current(), [])
            bond.update(assets=["bond_ai_01_02"], adv_play_types=[1])
            catalog["duplicate"] = bond
            with self.assertRaisesRegex(ValueError, "shared"):
                current()
            del catalog["duplicate"]
            bond["references"][0]["table"] = "Other"
            with self.assertRaisesRegex(ValueError, "relationship"):
                current()


if __name__ == "__main__":
    unittest.main()
