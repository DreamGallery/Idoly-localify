"""Compile production policies/adapters and exercise optional hook failures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
class PreviewRuntimeTests(unittest.TestCase):
    def test_preview_policies_and_adapter_boundaries(self):
        work = ROOT / '.agent-local' / 'native-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as temporary:
            for name in ('test_inline_hook', 'test_preview_access_policy', 'test_preview_adapters'):
                with self.subTest(name=name):
                    binary = Path(temporary) / name
                    subprocess.run(['c++', '-std=c++17', '-O1', '-pthread',
                                    '-I', str(ROOT / 'app/src/main/cpp'),
                                    '-I', str(ROOT / 'app/src/main/cpp/third_party'),
                                    '-I', str(ROOT / 'tools/native_test_support'),
                                    str(ROOT / 'tools' / (name + '.cpp')),
                                    '-o', str(binary)], check=True, capture_output=True, text=True)
                    subprocess.run([str(binary)], check=True, capture_output=True, timeout=30)


if __name__ == '__main__':
    unittest.main()
