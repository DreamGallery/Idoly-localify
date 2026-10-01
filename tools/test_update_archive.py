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
            result = subprocess.run([JAVAC, '-d', str(root), str(ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify/UpdateArchive.java'), str(ROOT / 'tools/java-tests/UpdateArchiveTest.java'), str(ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify/UpdatePolicy.java'), str(ROOT / 'tools/java-tests/UpdatePolicyTest.java'), str(ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify/UpdateVersion.java'), str(ROOT / 'tools/java-tests/UpdateVersionTest.java')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            for test in ('UpdateArchiveTest', 'UpdatePolicyTest', 'UpdateVersionTest'):
                result = subprocess.run([JAVA, '-cp', str(root), 'io.github.dreamgallery.idoly.localify.' + test, str(root)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
