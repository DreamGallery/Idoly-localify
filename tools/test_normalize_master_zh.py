import json
from pathlib import Path
import tempfile
import unittest

from normalize_master_zh import normalize


class NormalizeMasterZhTests(unittest.TestCase):
    def test_only_translation_values_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            value = {"1": {"description": "月のテンペストが輝く"}}
            (orig / "Story.json").write_text(json.dumps(value), encoding="utf-8")
            (zh / "Story.json").write_text(json.dumps({
                "1": {"description": "月光風暴閃耀"}}), encoding="utf-8")
            self.assertEqual(normalize(orig, zh,
                                       lambda text: text.replace("風", "风").replace("閃", "闪")),
                             (1, 1))
            self.assertEqual(json.loads((zh / "Story.json").read_text()),
                             {"1": {"description": "月光风暴闪耀"}})
            self.assertEqual(json.loads((orig / "Story.json").read_text()), value)


if __name__ == "__main__":
    unittest.main()
