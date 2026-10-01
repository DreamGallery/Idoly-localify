from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MasterBlobMatcherTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "A C++ compiler is required")
    def test_binary_patch_parser_and_exact_match(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "matcher-test"
            fixture = Path(directory) / "patches.bin"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "app/src/main/cpp"),
                            str(ROOT / "tools/test_master_blob_matcher.cpp"),
                            "-o", str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary), str(fixture)], check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
