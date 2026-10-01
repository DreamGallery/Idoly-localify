import unittest
from story_csv_paths import classify

class StoryCsvPathsTests(unittest.TestCase):
    def test_existing_classification_contract(self):
        examples = {
            'adv_card_mna_53_01.txt': 'card/mna/53/adv_card_mna_53_01.csv',
            'adv_card_ski_54_01_short.txt': 'card/ski/54/adv_card_ski_54_01_short.csv',
            'adv_event_2207_01_03.txt': 'event/2207/01/adv_event_2207_01_03.csv',
            'adv_event_excursion_moon_02_01.txt': 'event/excursion/moon/02/adv_event_excursion_moon_02_01.csv',
            'adv_group_moon_01_02.txt': 'group/月のテンペスト/01/adv_group_moon_01_02.csv',
            'adv_hbd_06_ski_01.txt': 'hbd/ski/06/adv_hbd_06_ski_01.csv',
            'adv_userhbd_01_mna.txt': 'hbd/mna/01/adv_userhbd_01_mna.csv',
            'adv_love_2026_01_02.txt': 'love/2026/01/adv_love_2026_01_02.csv',
            'adv_main_01_02_03.txt': 'main/01/02/adv_main_01_02_03.csv',
        }
        for name, expected in examples.items():
            with self.subTest(name=name): self.assertEqual(classify(name).as_posix(), expected)
    def test_unsafe_filename_rejected(self):
        with self.assertRaises(ValueError): classify('../adv_main_01.txt')
