"""Include the card story alias regression in the standard workspace checks."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class StorySwapTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "A C++ compiler is required")
    def test_card_alias_scope_short_version_and_invalid_state(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "story-swap-test"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "app/src/main/cpp"),
                            "-I", str(ROOT / "app/src/main/cpp/third_party"),
                            str(ROOT / "tools/test_story_swap.cpp"), "-o", str(binary)],
                           check=True, capture_output=True)
            subprocess.run([str(binary), str(Path(directory) / "story-swap.json")],
                           check=True, capture_output=True)


if __name__ == "__main__":
    unittest.main()
