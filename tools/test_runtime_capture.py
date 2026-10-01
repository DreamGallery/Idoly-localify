"""Fresh runtime captures must be append-only and bound to the chosen AVD."""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import runtime_capture


class RuntimeCaptureTests(unittest.TestCase):
    def test_extracts_only_new_valid_records(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = (json.dumps({"kind": "text", "source": "旧文本"},
                                   ensure_ascii=False) + "\n").encode()
            added = (json.dumps({"kind": "i18n", "key": "new.key", "source": "新文本"},
                                ensure_ascii=False) + "\n").encode()
            pulls = iter((baseline, baseline + added))

            def fake_pull(_adb, _serial, path):
                path.write_bytes(next(pulls))

            session = root / "session.json"
            output = root / "delta.jsonl"
            with patch.object(runtime_capture, "assert_expected_avd") as guard, \
                 patch.object(runtime_capture, "pull_dump", side_effect=fake_pull):
                runtime_capture.start("adb", "emulator-5560", "Idoly_Test", session)
                result = runtime_capture.finish("adb", "emulator-5560", "Idoly_Test",
                                                session, output)
            self.assertEqual(result["records"], 1)
            self.assertEqual(output.read_bytes(), added)
            self.assertFalse(session.exists())
            self.assertEqual(guard.call_count, 2)

    def test_rotated_dump_keeps_session_for_retry_or_abort(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = b'{"kind":"text","source":"before"}\n'
            changed = b'{"kind":"text","source":"other!"}\n'
            pulls = iter((baseline, changed))
            with patch.object(runtime_capture, "assert_expected_avd"), \
                 patch.object(runtime_capture, "pull_dump",
                              side_effect=lambda _a, _s, path: path.write_bytes(next(pulls))):
                session = root / "session.json"
                runtime_capture.start("adb", "emulator-5560", "Idoly_Test", session)
                with self.assertRaisesRegex(ValueError, "rotated or edited"):
                    runtime_capture.finish("adb", "emulator-5560", "Idoly_Test",
                                           session, root / "delta.jsonl")
            self.assertTrue(session.exists())
            self.assertFalse((root / "delta.jsonl").exists())

    def test_wrong_avd_stops_before_reading_game_data(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(runtime_capture, "assert_expected_avd",
                              side_effect=ValueError("deployment refused")), \
                 patch.object(runtime_capture, "pull_dump") as pull:
                with self.assertRaisesRegex(ValueError, "deployment refused"):
                    runtime_capture.start("adb", "emulator-5554", "Idoly_Test",
                                          Path(directory) / "session.json")
                pull.assert_not_called()


if __name__ == "__main__":
    unittest.main()
