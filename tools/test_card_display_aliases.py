import copy
import unittest

from card_display_aliases import derive


class CardDisplayAliasesTests(unittest.TestCase):
    def setUp(self):
        self.cards = {"card-yuu-04-test-00": {"name": "至福の時間"},
                      "card-yuu-03-test-00": {"name": "自主トレ日和"}}
        self.zh = {"card-yuu-04-test-00": {"name": "至福时光"},
                   "card-yuu-03-test-00": {"name": "自主训练的好日子"}}
        self.ref = {"category": "card", "card_id": "card-yuu-04-test-00",
                    "character_id": "char-yuu", "character_code": "yuu",
                    "character_name": "鈴村優"}
        self.catalog = {"stories": {"story": {"references": [self.ref]}}}
        self.names = {"鈴村優": "铃村优"}

    def build(self):
        return derive(self.cards, self.zh, self.catalog, self.names)

    def test_mixed_and_original_history_labels_keep_spaces_and_stars(self):
        aliases = self.build()
        for title in ("至福の時間", "至福时光"):
            for name in ("鈴村優", "铃村优"):
                source = f"{title}　{name}　★4"
                if source != "至福时光　铃村优　★4":
                    self.assertEqual(aliases[source], "至福时光　铃村优　★4")
        self.assertNotIn("至福时光　铃村优　★4", aliases)
        self.assertEqual(aliases["至福时光　鈴村優"], "至福时光　铃村优")

    def test_low_rarity_card_without_story_uses_verified_character_code(self):
        self.assertEqual(self.build()["自主训练的好日子　鈴村優　★3"],
                         "自主训练的好日子　铃村优　★3")

    def test_unknown_names_titles_numbers_and_formats_are_not_generated(self):
        self.cards["card-unknown-03-test-00"] = {"name": "不明"}
        self.cards["future-yuu"] = {"name": "新形式"}
        aliases = self.build()
        for source in ("玩家昵称　鈴村優　★4", "至福时光　陌生玩家　★4",
                       "至福时光 鈴村優 ★4", "至福时光　鈴村優　★6",
                       "至福时光　鈴村優　★04", "新形式　鈴村優",
                       "不明　鈴村優"):
            self.assertNotIn(source, aliases)

    def test_conflicting_character_relationship_stops_build(self):
        other = copy.deepcopy(self.ref)
        other["character_name"] = "赤崎こころ"
        self.catalog["stories"]["story"]["references"].append(other)
        with self.assertRaisesRegex(ValueError, "Conflicting card character"):
            self.build()

    def test_missing_card_translation_can_still_translate_known_name(self):
        self.zh = {}
        self.assertEqual(self.build()["至福の時間　鈴村優　★4"],
                         "至福の時間　铃村优　★4")

    def test_ambiguous_full_labels_do_not_choose_by_record_order(self):
        self.cards["card-yuu-05-other-00"] = {"name": "至福の時間"}
        self.zh["card-yuu-05-other-00"] = {"name": "另一译文"}
        with self.assertRaisesRegex(ValueError, "Conflicting card display alias"):
            self.build()


if __name__ == "__main__":
    unittest.main()
