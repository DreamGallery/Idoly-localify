"""Exercise the production encrypted account container using synthetic files only."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AccountArchiveTests(unittest.TestCase):
    def test_authenticated_container(self):
        java = Path(os.environ.get('JAVA_HOME', '/Applications/Android Studio.app/Contents/jbr/Contents/Home')) / 'bin'
        cache = Path(os.environ.get('GRADLE_USER_HOME', str(Path.home() / '.gradle'))) / 'caches/modules-2/files-2.1'
        jars = list((cache / 'com.google.crypto.tink/tink-android/1.21.0').glob('*/*.jar'))
        if not jars:
            self.skipTest('Run the Android build once to resolve Tink')
        work = ROOT / '.agent-local/account-tests'
        work.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=work) as directory:
            cp = os.pathsep.join([directory, *map(str, jars)])
            sources = [ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify/AccountArchive.java',
                       ROOT / 'tools/java-tests/AccountArchiveTest.java']
            subprocess.run([str(java / 'javac'), '-cp', cp, '-d', directory, *map(str, sources)], check=True, capture_output=True, text=True)
            result = subprocess.run([str(java / 'java'), '-cp', cp, 'io.github.dreamgallery.idoly.localify.AccountArchiveTest', directory], capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stderr)

if __name__ == '__main__':
    unittest.main()
