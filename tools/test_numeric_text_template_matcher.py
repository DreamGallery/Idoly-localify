from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NumericTextTemplateMatcherTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which('c++'), 'A C++ compiler is required')
    def test_strict_ascii_number_substitution_and_rendered_font_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'numeric-template-test'
            subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tools/test_numeric_text_template_matcher.cpp'),
                            '-o', str(binary)], check=True, capture_output=True)
            subprocess.run([str(binary)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
