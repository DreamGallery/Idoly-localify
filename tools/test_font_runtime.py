"""Run the portable runtime font regressions in the standard workspace gate."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("c++"), "A C++ compiler is required")
class FontRuntimeTests(unittest.TestCase):
    def run_fixture(self, name):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / name
            subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 str(ROOT / "tools" / f"{name}.cpp"), "-o", str(binary)],
                check=True, capture_output=True,
            )
            subprocess.run([str(binary)], check=True, capture_output=True)

    def test_complete_label_font_routing(self):
        self.run_fixture("test_mixed_font_router")

    def test_default_ui_font_and_original_design_exceptions(self):
        self.run_fixture("test_ui_font_policy")

    def test_love_episode_label_scope_and_dynamic_number(self):
        self.run_fixture("test_love_episode_label")

    def test_panorama_font_scope_and_paired_layout(self):
        self.run_fixture("test_panorama_font_labels")

    def test_support_layout_survives_whole_label_font_mode(self):
        self.run_fixture("test_label_layout_modes")

    def test_constrained_title_metrics(self):
        self.run_fixture("test_constrained_font_metrics")

    def test_existing_new_missing_and_failed_glyph_prewarm(self):
        self.run_fixture("test_font_prewarm_coverage")

    def test_renderer_submission_and_original_font_restore(self):
        self.run_fixture("test_tmp_renderer_submit")


if __name__ == "__main__":
    unittest.main()
