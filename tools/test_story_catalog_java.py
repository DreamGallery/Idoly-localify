"""Check the Android reader against legacy and extended update manifests."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class StoryCatalogJavaTests(unittest.TestCase):
    def test_catalog_validation_update_compatibility_and_local_priority(self):
        sdk = Path(os.environ.get('ANDROID_HOME', Path.home() / 'Library/Android/sdk'))
        sources = sorted((sdk / 'sources').glob('android-*/org/json'))
        jdk = Path(os.environ.get('JAVA_HOME', '/Applications/Android Studio.app/Contents/jbr/Contents/Home'))
        javac, java = jdk / 'bin/javac', jdk / 'bin/java'
        if not sources or not javac.is_file():
            self.skipTest('Android SDK sources and JDK required')
        scratch = ROOT / '.agent-local/test-runtime'
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            temp = Path(directory)
            inputs = []
            # Reuse the installed SDK JSON implementation; strip platform-only
            # annotations so these production validators can run on the host JVM.
            for source in sources[-1].glob('*.java'):
                text = source.read_text()
                text = re.sub(r'^import (?:static )?(?:android\.|libcore\.).*;\n', '', text, flags=re.M)
                text = re.sub(r'@(?:UnsupportedAppUsage|NonNull|Nullable)\b', '', text)
                text = re.sub(r'@SystemApi\([^)]*\)', '', text)
                target = temp / source.name
                target.write_text(text)
                inputs.append(str(target))
            inputs += [str(ROOT / 'app/src/main/java/io/github/dreamgallery/idoly/localify/StoryCatalog.java'),
                       str(ROOT / 'tools/java-tests/StoryCatalogTest.java')]
            result = subprocess.run([str(javac), '-d', str(temp), *inputs], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(java), '-ea', '-cp', str(temp),
                                     'io.github.dreamgallery.idoly.localify.StoryCatalogTest', str(temp)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
