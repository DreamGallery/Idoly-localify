import unittest

from agency_name import normalize


class AgencyNameTests(unittest.TestCase):
    def test_case_accent_and_literal_japanese_variants(self):
        source = "プレタポルテの事務所"
        for value in ("PRET-A-PORTER", "PRÊT-À-PORTE", "pret-a-porter",
                      "Prete-a-Porter", "プレタポルテ", "普拉达波尔特"):
            with self.subTest(value=value):
                self.assertEqual(normalize(source, value), "Prêt-à-Porter")

    def test_preserves_other_fashion_discussion(self):
        self.assertEqual(normalize("高級な衣装", "高级定制的衣服"), "高级定制的衣服")


if __name__ == "__main__":
    unittest.main()
