from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image
from check_runtime_images import read_directory, validate


class RuntimeImageTests(unittest.TestCase):
    def fixture(self, **changes):
        image = BytesIO()
        Image.new('RGBA', (8, 16), 'white').save(image, format='PNG')
        png = image.getvalue()
        item = {'name': 'atlas-name', 'file': 'atlas-0.png', 'width': 8, 'height': 16,
                'sha256': sha256(png).hexdigest(), **changes}
        return {'atlas-0.png': png, 'manifest.json': json.dumps(
            {'schema_version': 1, 'textures': [item]}).encode()}

    def test_valid_payload_and_corrupt_pixels(self):
        files = self.fixture()
        self.assertEqual(len(validate(files)['textures']), 1)
        files['atlas-0.png'] += b'changed'
        with self.assertRaisesRegex(ValueError, 'checksum'):
            validate(files)

    def test_dimensions_and_unlisted_files_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'dimensions'):
            validate(self.fixture(width=9))
        files = self.fixture()
        files['capture.txt'] = b'private data'
        with self.assertRaisesRegex(ValueError, 'Unexpected'):
            validate(files)

    def test_unsafe_and_duplicate_names_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Invalid'):
            validate(self.fixture(file='../atlas-0.png'))
        files = self.fixture()
        manifest = json.loads(files['manifest.json'])
        manifest['textures'] *= 2
        files['manifest.json'] = json.dumps(manifest).encode()
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            validate(files)

    def test_symlinked_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name, data in self.fixture().items():
                (root / name).write_bytes(data)
            self.assertEqual(len(read_directory(root)), 2)
            (root / 'link.png').symlink_to(root / 'atlas-0.png')
            with self.assertRaisesRegex(ValueError, 'Invalid'):
                read_directory(root)
