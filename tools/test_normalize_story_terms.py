import unittest

from normalize_story_terms import normalize


class NormalizeStoryTermsTests(unittest.TestCase):
    def test_full_unit_names_follow_canonical_glossary(self):
        self.assertEqual(normalize("月のテンペストとサニーピース",
                                   "月之暴风雨和Sunny Peace")[0],
                         "月光风暴和SUNNY PEACE")

    def test_abbreviations_and_mistranscriptions(self):
        self.assertEqual(normalize("月ストとサニピとトリエル",
                                   "月Stou、SunnyPiece和TRINEL")[0],
                         "月光风暴、SUNNY PEACE和TRINITYAiLE")
        self.assertEqual(normalize("スリクスとリズノワ",
                                   "斯利克斯和莉兹诺瓦")[0],
                         "IIIX和LizNoir")

    def test_unrelated_source_is_unchanged(self):
        self.assertEqual(normalize("今夜は月がきれい", "月之风暴的夜晚")[0],
                         "月之风暴的夜晚")

    def test_retained_japanese_terms_follow_approved_glossary(self):
        self.assertEqual(normalize("トリエルとぱじゃパ！", "トリエル和ぱじゃパ！")[0],
                         "TRINITYAiLE和睡衣派对")
        self.assertEqual(normalize("萌え萌えキュンと言った", "说了萌え萌えキュン")[0],
                         "说了萌萌啾")

    def test_agency_name_replaces_mistranslations_only_with_matching_source(self):
        self.assertEqual(normalize("プレタポルテにいた頃", "在高级定制时")[0],
                         "在Prêt-à-Porter时")
        self.assertEqual(normalize("プレタポルテへ", "去普雷塔波尔特和Pret-a-Porter")[0],
                         "去Prêt-à-Porter和Prêt-à-Porter")
        self.assertEqual(normalize("高級な服", "高级定制的衣服")[0],
                         "高级定制的衣服")


if __name__ == "__main__":
    unittest.main()
