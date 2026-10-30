"""Run the same archive decoder used by Android without an emulator or network."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JAVAC = str(Path(os.environ['JAVA_HOME']) / 'bin/javac') if os.environ.get('JAVA_HOME') else shutil.which('javac')
JAVA = str(Path(os.environ['JAVA_HOME']) / 'bin/java') if os.environ.get('JAVA_HOME') else shutil.which('java')

@unittest.skipUnless(JAVAC and JAVA, 'A JDK is required')
class UpdateArchiveTests(unittest.TestCase):
    def test_archive_validation_and_selective_staging(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cases = ('UpdateArchive', 'UpdatePolicy', 'UpdateVersion', 'NativeResourceValidation')
            sources = []
            for name in cases:
                sources.extend([str(ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify' / (name + '.java')),
                                str(ROOT / 'tools/java-tests' / (name + 'Test.java'))])
            result = subprocess.run([JAVAC, '-d', str(root), *sources], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for name in cases:
                result = subprocess.run([JAVA, '-cp', str(root), 'io.github.dreamgallery.idoly.localify.' + name + 'Test', str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
