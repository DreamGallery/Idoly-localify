"""Runtime capture matching must use only observed story text variants."""

import json
from pathlib import Path
import tempfile
import unittest

from audit_runtime_story import audit


class RuntimeStoryAuditTests(unittest.TestCase):
    def test_matches_parenthesized_story_and_rendered_newline(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            story = root / "story"
            story.mkdir()
            (story / "adv_test.json").write_text(json.dumps({
                "translations": {"1:text:1": r"（第一行\n第二行）",
                                 "2:text:1": "普通正文"},
            }, ensure_ascii=False), encoding="utf-8")
            dump = root / "dump.jsonl"
            dump.write_text(json.dumps({"kind": "text", "source": "第一行\n第二行"},
                                       ensure_ascii=False) + "\n" +
                            json.dumps({"kind": "text", "source": "无关文本"},
                                       ensure_ascii=False) + "\n", encoding="utf-8")
            report = audit(dump, story)
            self.assertEqual(report["matched_rendered_text"], 1)
            self.assertEqual(report["matches"], [{
                "rendered": "第一行\n第二行",
                "locations": [{"file": "adv_test.json", "field": "1:text:1"}],
            }])

    def test_rejects_corrupt_capture_line(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dump = root / "dump.jsonl"
            dump.write_text('{"kind":"text","source":', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "dump.jsonl:1: invalid JSON"):
                audit(dump, root)


if __name__ == "__main__":
    unittest.main()
