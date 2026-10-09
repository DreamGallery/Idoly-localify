"""Exercise the production cleanup selector and deletion checks with real files."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JAVA_HOME = Path(os.environ['JAVA_HOME']) if os.environ.get('JAVA_HOME') else None
JAVAC = str(JAVA_HOME / 'bin/javac') if JAVA_HOME else shutil.which('javac')
JAVA = str(JAVA_HOME / 'bin/java') if JAVA_HOME else shutil.which('java')


@unittest.skipUnless(JAVAC and JAVA, 'A JDK is required')
class LocalDataFilesTests(unittest.TestCase):
    def test_cleanup_preserves_unrelated_files_and_rejects_links(self):
        with tempfile.TemporaryDirectory() as directory:
            package = ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify'
            compile_result = subprocess.run([JAVAC, '-d', directory,
                str(package / 'LocalDataFiles.java'),
                str(ROOT / 'tools/java-tests/LocalDataFilesTest.java')], capture_output=True, text=True)
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            result = subprocess.run([JAVA, '-cp', directory,
                'io.github.dreamgallery.idoly.localify.LocalDataFilesTest', directory],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
