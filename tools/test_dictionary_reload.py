"""Exercise the production native loader, including deferred startup settings."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def build_loader_test(directory):
    source = (ROOT / 'app/src/main/cpp/localify.cpp').read_text()
    start = source.index('bool loadDictionary(')
    end = source.index('\n} // namespace', start)
    (directory / 'dictionary_loader_under_test.inc').write_text(source[start:end])
    binary = directory / 'dictionary-reload-test'
    subprocess.run(['c++', '-std=c++17', '-O1', '-I', str(directory),
                    '-I', str(ROOT / 'app/src/main/cpp'),
                    '-I', str(ROOT / 'app/src/main/cpp/third_party'),
                    str(ROOT / 'tools/test_dictionary_reload.cpp'), '-o', str(binary)],
                   check=True, capture_output=True)
    return binary


@unittest.skipUnless(shutil.which('c++'), 'C++ compiler required')
class DictionaryReloadTests(unittest.TestCase):
    def test_valid_configuration_changes_wait_for_restart_and_invalid_input_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            binary = build_loader_test(directory)
            subprocess.run([str(binary), str(directory)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
