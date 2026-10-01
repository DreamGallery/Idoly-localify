"""Relocated checkouts and separate data folders work without generic src imports."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from workspace_paths import toolkit_root, translator_root

ROOT = Path(__file__).resolve().parents[1]


class WorkspacePathsTests(unittest.TestCase):
    def test_renamed_checkouts_and_independent_data_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory).resolve()
            for name, original in (("resource-tool", toolkit_root()),
                                   ("translation-tool", translator_root())):
                shutil.copytree(original / "src", base / name / "src",
                                ignore=shutil.ignore_patterns("__pycache__"))
            source = base / "raw-story"
            source.mkdir()
            (source / "adv_test.txt").write_text('[message text=こんにちは]\n', encoding="utf-8")
            environment = {k: v for k, v in os.environ.items() if not k.startswith("IDOLY_")}
            environment.update(PYTHONPATH=str(ROOT / "tools"), PYTHONDONTWRITEBYTECODE="1",
                               IDOLY_TOOLKIT_DIR=str(base / "resource-tool"),
                               IDOLY_TRANSLATOR_DIR=str(base / "translation-tool"),
                               IDOLY_STORY_SOURCE_DIR=str(source),
                               IDOLY_STORY_CSV_DIR=str(base / "review"),
                               IDOLY_STORY_OUTPUT_DIR=str(base / "published"))
            script = '''import json, sys
from pathlib import Path
from pipeline import Pipeline
from workspace_paths import load_module
pipeline = Pipeline(Path("independent-plugin"))
assert pipeline.export() == 1
load_module("Hoshimi_Teleprompter", "src.csv_workflow")
load_module("Hoshimi_Teleprompter", "src.legal_workflow")
assert "src" not in sys.modules
print(json.dumps([str(pipeline.csv_dir), str(pipeline.txt_dir)]))
'''
            result = subprocess.run([sys.executable, "-c", script], cwd=base,
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout), [str(base / "review"), str(base / "published")])
            self.assertTrue((base / "review/adv_test.csv").is_file())
            result = subprocess.run([sys.executable, str(ROOT / "tools/pipeline.py"), "export",
                                     "--csv-dir", str(base / "review-via-cli")], cwd=base,
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((base / "review-via-cli/adv_test.csv").is_file())


if __name__ == "__main__":
    unittest.main()
