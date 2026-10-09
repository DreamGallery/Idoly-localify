"""Validate the reviewed runtime image payload before building or publishing."""
from __future__ import annotations

import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
import re

from PIL import Image

DEFAULT = Path(__file__).resolve().parents[1] / 'app/src/main/assets/idoly-localify/images'


def validate(files):
    raw = files.get('manifest.json', b'')
    if not raw or len(raw) > 128 * 1024:
        raise ValueError('Missing or oversized runtime image manifest')
    manifest = json.loads(raw)
    textures = manifest.get('textures')
    if (manifest.get('schema_version') != 1 or not isinstance(textures, list)
            or not 1 <= len(textures) <= 64):
        raise ValueError('Invalid runtime image manifest')
    names, paths = set(), {'manifest.json'}
    for texture in textures:
        name, path = texture.get('name'), texture.get('file')
        width, height = texture.get('width'), texture.get('height')
        digest = texture.get('sha256')
        if (not isinstance(name, str) or not 1 <= len(name) <= 256 or name in names
                or not isinstance(path, str) or not re.fullmatch(r'atlas-[0-9]+\.png', path)
                or path in paths or not isinstance(digest, str)
                or not re.fullmatch(r'[0-9a-f]{64}', digest)
                or any(type(v) is not int or not 1 <= v <= 4096 for v in (width, height))):
            raise ValueError('Invalid or duplicate runtime image entry')
        names.add(name)
        paths.add(path)
        png = files.get(path, b'')
        if not png or len(png) > 32 * 1024 * 1024 or sha256(png).hexdigest() != digest:
            raise ValueError('Runtime image checksum mismatch')
        with Image.open(BytesIO(png)) as image:
            if image.format != 'PNG' or image.mode != 'RGBA' or image.size != (width, height):
                raise ValueError('Runtime image format or dimensions mismatch')
            image.verify()
    if set(files) != paths:
        raise ValueError('Unexpected runtime image files')
    return manifest


def read_directory(directory):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Runtime image directory is missing or linked')
    files = {}
    for path in directory.iterdir():
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 32 * 1024 * 1024:
            raise ValueError('Invalid runtime image file')
        files[path.name] = path.read_bytes()
    validate(files)
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path, default=DEFAULT)
    args = parser.parse_args()
    files = read_directory(args.directory)
    print(f'Verified {len(files) - 1} runtime image atlases')
