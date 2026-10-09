#!/usr/bin/env python3
"""Build verified runtime atlas PNGs from original APK textures and region patches."""
import argparse
from hashlib import sha256
from io import BytesIO
import json
from pathlib import Path
from zipfile import ZipFile

from PIL import Image
import UnityPy
from check_runtime_images import validate


def build(base, patches, output):
    base, patches, output = Path(base), Path(patches), Path(output)
    manifest = json.loads(patches.read_text())
    if manifest.get('schema_version') != 2:
        raise ValueError('Expected region patch schema 2')
    result = {'schema_version': 1, 'textures': []}
    pending = {}
    with ZipFile(base) as apk:
        for asset in manifest['assets']:
            raw = apk.read(asset['entry'])
            if sha256(raw).hexdigest() != asset['source_sha256']:
                raise ValueError('Original atlas asset does not match reviewed source')
            objects = {o.path_id: o for o in UnityPy.load(raw).objects}
            for item in asset['textures']:
                texture = objects[item['path_id']].read()
                if texture.m_Name != item['name']:
                    raise ValueError('Texture identity mismatch')
                image = texture.image.convert('RGBA')
                for region in item['regions']:
                    path = (patches.parent / region['png']).resolve()
                    if not path.is_relative_to(patches.parent.resolve()):
                        raise ValueError('Unsafe patch path')
                    png = path.read_bytes()
                    if sha256(png).hexdigest() != region['png_sha256']:
                        raise ValueError('Region checksum mismatch')
                    x0, y0, x1, y1 = region['box']
                    if not (0 <= x0 < x1 <= image.width and 0 <= y0 < y1 <= image.height):
                        raise ValueError('Region bounds invalid')
                    with Image.open(BytesIO(png)) as patch:
                        if patch.size != (x1-x0, y1-y0):
                            raise ValueError('Region size invalid')
                        image.paste(patch.convert('RGBA'), (x0, y0))
                if sha256(image.tobytes()).hexdigest() != item['rgba_sha256']:
                    raise ValueError('Atlas pixels differ from reviewed image')
                encoded = BytesIO()
                image.save(encoded, format='PNG')
                filename = f'atlas-{len(pending)}.png'
                pending[filename] = encoded.getvalue()
                result['textures'].append({'name': item['name'], 'width': image.width, 'height': image.height,
                                          'file': filename, 'sha256': sha256(pending[filename]).hexdigest()})
    if len({t['name'] for t in result['textures']}) != len(pending):
        raise ValueError('Duplicate atlas name')
    pending['manifest.json'] = (json.dumps(result, ensure_ascii=False, indent=2)+'\n').encode()
    validate(pending)
    output.mkdir(parents=True, exist_ok=True)
    for name, raw in pending.items():
        (output / name).write_bytes(raw)
    for old in output.glob('atlas-*.png'):
        if old.name not in pending:
            old.unlink()
    print(f'Built {len(pending) - 1} verified runtime atlases')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--patches', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.base, args.patches, args.output)
