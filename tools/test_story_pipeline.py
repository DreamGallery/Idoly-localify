"""End-to-end TXT → CSV → mock model → TXT check for the primary workflow."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


ROOT = Path(__file__).resolve().parents[1]
from workspace_paths import toolkit_root, translator_root

TOOLKIT = toolkit_root()
TRANSLATOR = translator_root()


@unittest.skipUnless(all(path.is_dir() for path in (TOOLKIT, TRANSLATOR)),
                     "The resource and translation repositories must be present")
class StoryPipelineTests(unittest.TestCase):
    def test_lossless_roundtrip_with_translated_fields(self):
        translations = {
            "一話": "第一话",
            r"{user}、おはよう\n今日も": r"{user}，早上好\n今天也一样",
            "朝": "早晨",
            "はい": "是",
            "いいえ": "否",
        }

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers["Content-Length"])
                request = json.loads(self.rfile.read(length))
                task = json.loads(request["messages"][1]["content"])
                response = {"choices": [{"message": {"content": json.dumps(
                    {"translations": [{"id": line["id"],
                                       "translation": translations[line["source"]]}
                                      for line in task["lines"]]}, ensure_ascii=False)}}]}
                body = json.dumps(response, ensure_ascii=False).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_args):
                pass

        try:
            server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        except PermissionError:
            self.skipTest("Local loopback sockets are restricted in this sandbox")
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)

        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            raw = work / "raw"
            csv_dir = work / "csv"
            output = work / "output"
            raw.mkdir()
            original = raw / "adv_test.txt"
            original.write_bytes(("[title title=一話]\r\n"
                                  r"[message text={user}、おはよう\n今日も name=長瀬麻奈 clip=\{x:1\}]" + "\r\n"
                                  "[narration text=朝]\r\n"
                                  "[choicegroup choices=[choice text=はい] choices=[choice text=いいえ]]\r\n"
                                  "[timeline]").encode("utf-8"))
            glossary = work / "names.json"
            glossary.write_text(json.dumps({"長瀬麻奈": "长濑麻奈"}, ensure_ascii=False),
                                encoding="utf-8")

            def run(project, *args, env=None):
                result = subprocess.run([sys.executable, str(project / "main.py"), *map(str, args)],
                                        cwd=project, env=env, text=True,
                                        capture_output=True, check=False)
                self.assertEqual(result.returncode, 0,
                                 f"{project.name}: {result.stdout}\n{result.stderr}")

            result = subprocess.run([sys.executable, str(TOOLKIT / "adv_csv.py"),
                                     "export", "--source-dir", str(raw), "--csv-dir",
                                     str(csv_dir), "--file", original.name],
                                    cwd=TOOLKIT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            environment = os.environ.copy()
            environment["OPENAI_API_KEY"] = "local-test-only"
            run(TRANSLATOR, "translate", "--csv-dir", csv_dir, "--source-dir", raw,
                "--glossary", glossary, "--file", "adv_test.csv", "--batch-size", "2",
                "--api-base", f"http://127.0.0.1:{server.server_port}/v1",
                "--model", "mock-model", env=environment)
            merged = subprocess.run([sys.executable, str(TOOLKIT / "adv_csv.py"),
                                     "merge", "--source-dir", str(raw),
                                     "--csv-dir", str(csv_dir),
                                     "--output-dir", str(output),
                                     "--file", original.name,
                                     "--name-glossary", str(glossary)],
                                    cwd=TOOLKIT, text=True, capture_output=True)
            self.assertEqual(merged.returncode, 0, merged.stderr)

            localized = (output / original.name).read_bytes().decode("utf-8")
            self.assertIn("title=第一话", localized)
            self.assertIn(r"text={user}，早上好\n今天也一样 name=长濑麻奈", localized)
            self.assertIn("[narration text=早晨]", localized)
            self.assertIn("[choicegroup choices=[choice text=是] choices=[choice text=否]]",
                          localized)
            self.assertIn(r"clip=\{x:1\}", localized)
            self.assertTrue(localized.endswith("[timeline]"))


if __name__ == "__main__":
    unittest.main()
