import json
from pathlib import Path
import tempfile
import unittest

from audit_master_runtime import audit


class MasterRuntimeAuditTests(unittest.TestCase):
    def test_distinguishes_matching_retained_changed_and_other_translation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            orig, zh = root / "orig", root / "zh"
            orig.mkdir()
            zh.mkdir()
            (orig / "Music.json").write_text(json.dumps({
                "1": {"singer": "星見プロダクション",
                      "composer": "星見プロダクション",
                      "title": "新曲"},
                "2": {"title": "未翻訳"}}, ensure_ascii=False), encoding="utf-8")
            (zh / "Music.json").write_text(json.dumps({
                "1": {"singer": "星见Production",
                      "composer": "星見プロダクション",
                      "title": "新歌"},
                "2": {"title": ""}}, ensure_ascii=False), encoding="utf-8")
            dictionary = root / "dict.json"
            dictionary.write_text(json.dumps({"text": {
                "星見プロダクション": "星见Production", "新曲": "新曲目"}},
                ensure_ascii=False), encoding="utf-8")
            result = audit(orig, zh, dictionary, details=True)
            self.assertEqual(result["counts"], {
                "matching": 1, "retained_changed": 1,
                "translated_differently": 1, "untranslated": 1})
            self.assertEqual({item["field_path"] for item in result["mismatches"]},
                             {"composer", "title"})


if __name__ == "__main__":
    unittest.main()
