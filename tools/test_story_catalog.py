"""MasterDB relationship index must preserve explicit story categories."""

import json
from pathlib import Path
import tempfile
import unittest

from story_catalog import TABLES, build_catalog


class StoryCatalogTests(unittest.TestCase):
    def test_card_and_company_stories_are_indexed_by_asset(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tables = {name: [] for name in TABLES}
            tables["Story"] = [
                {"id": "st-card", "name": "Card", "advAssetIds": ["card-01"]},
                {"id": "st-company", "name": "Company", "advAssetIds": ["bond-01"]},
                {"id": "st-unreferenced", "name": "Unknown", "advAssetIds": ["other-01"]},
            ]
            tables["Card"] = [{"id": "card-ai", "characterId": "char-ai", "stories": [{"storyId": "st-card"}]}]
            tables["Character"] = [{"id": "char-ai", "assetId": "ai", "name": "小美山愛", "companyEnjoyStories": [
                {"number": 4, "storyId": "st-company", "assetId": "04"}]}]
            for name, rows in tables.items():
                (root / f"{name}.json").write_text(json.dumps(rows), encoding="utf-8")

            catalog = build_catalog(root)
            self.assertEqual(catalog["stories"]["st-card"]["categories"], ["card"])
            self.assertEqual(catalog["stories"]["st-card"]["references"][0], {
                "category": "card", "table": "Card", "card_id": "card-ai", "episode": 1,
                "character_id": "char-ai", "character_code": "ai", "character_name": "小美山愛"})
            self.assertEqual(catalog["stories"]["st-company"]["references"], [{
                "category": "company", "table": "Character", "character_id": "char-ai",
                "episode": 4, "asset_id": "04"}])
            self.assertEqual(catalog["stories"]["st-unreferenced"]["categories"], [])
            self.assertEqual(catalog["assets"]["card-01"], ["st-card"])

    def test_unknown_reference_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in TABLES:
                rows = [{"id": "card-ai", "stories": [{"storyId": "missing"}]}] \
                    if name == "Card" else []
                (root / f"{name}.json").write_text(json.dumps(rows), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unknown Story ID"):
                build_catalog(root)

    def test_incomplete_master_fetch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".master-fetch-in-progress.json").write_text("{}", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "fetch is incomplete"):
                build_catalog(root)

    def test_verified_fetch_cannot_use_a_modified_table(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in TABLES:
                (root / f"{name}.json").write_text("[]", encoding="utf-8")
            state = {"version": "v1", "complete_version": "v1", "tables": {
                name: {"sha256": "0" * 64} for name in TABLES}}
            (root / "master-fetch-state.json").write_text(json.dumps(state), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "differs from the verified MasterDB fetch"):
                build_catalog(root)


if __name__ == "__main__":
    unittest.main()
