#!/usr/bin/env python3
"""Prepare the pinned default font using a verified standalone bundle template.

This also upgrades an old runner's RHR bundle without a game APK. The template
is read-only; all non-font objects and Unity references stay unchanged.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MAX_FONT_BYTES = 64 * 1024 * 1024


def verify_font(data, spec):
    from fontTools.ttLib import TTFont
    if hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError('Default font SHA-256 mismatch')
    with TTFont(BytesIO(data)) as font:
        if (font['name'].getDebugName(1) != spec['family'] or
                font['name'].getDebugName(2) != spec['style'] or
                font['OS/2'].usWeightClass != spec['weight'] or not font.getBestCmap()):
            raise ValueError('Default font metadata mismatch')


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def resolve_font(spec, cache, font_path=None):
    if font_path is not None:
        data = font_path.read_bytes()
        verify_font(data, spec)
        return data
    cached = cache / (spec['sha256'] + '.otf')
    if cached.is_file():
        data = cached.read_bytes()
        verify_font(data, spec)
        return data
    source = spec.get('source', spec)
    if 'path' in source:
        relative = Path(source['path'])
        if (relative.is_absolute() or '..' in relative.parts or
                not (ROOT / relative).resolve().is_relative_to(ROOT.resolve()) or
                source.get('format') != 'gzip'):
            raise ValueError('Invalid packaged font source')
        packed = (ROOT / relative).read_bytes()
        if len(packed) > MAX_FONT_BYTES or hashlib.sha256(packed).hexdigest() != source['sha256']:
            raise ValueError('Packaged font source SHA-256 or size mismatch')
        with gzip.GzipFile(fileobj=BytesIO(packed)) as stream:
            data = stream.read(MAX_FONT_BYTES + 1)
        if len(data) > MAX_FONT_BYTES:
            raise ValueError('Packaged font exceeds size limit')
        verify_font(data, spec)
        atomic_write(cached, data)
        return data
    if not source.get('url'):
        raise ValueError('This font requires a local file; provide --font with the pinned font')
    source_cache = cache / (source['sha256'] + '.otf')
    if 'instance' in spec and source_cache.is_file():
        data = source_cache.read_bytes()
    else:
        if not source['url'].startswith('https://raw.githubusercontent.com/notofonts/noto-cjk/'):
            raise ValueError('Expected an official Noto HTTPS source')
        with urllib.request.urlopen(source['url'], timeout=120) as response:
            data = response.read(MAX_FONT_BYTES + 1)
    if len(data) > MAX_FONT_BYTES:
        raise ValueError('Default font download exceeds size limit')
    if hashlib.sha256(data).hexdigest() != source['sha256']:
        raise ValueError('Font source SHA-256 mismatch')
    if 'instance' in spec:
        from instantiate_noto import instantiate_font
        atomic_write(source_cache, data)
        data = instantiate_font(data, spec)
    verify_font(data, spec)
    atomic_write(cached, data)
    return data


def replace_bundle_font(raw, replacement, *, font_name=None, bundle_name=None):
    import UnityPy
    from build_font_bundle import verify
    from patch_font import INDEPENDENT_FONT_NAME, replacement_family

    environment = UnityPy.load(raw)
    fonts = [obj for obj in environment.objects if obj.type.name == 'Font']
    if len(fonts) != 1:
        raise ValueError('Expected a standalone font bundle template')
    tree = fonts[0].read_typetree()
    verify(raw, bytes(tree['m_FontData']), font_name=tree['m_Name'])
    font_name = font_name or INDEPENDENT_FONT_NAME
    if not font_name.isascii() or not all(c.isalnum() or c in '-_' for c in font_name):
        raise ValueError('Invalid Unity font asset name')
    tree['m_Name'] = font_name
    tree['m_FontData'] = list(replacement)
    tree['m_FontNames'] = [replacement_family(replacement)]
    fonts[0].save_typetree(tree)
    # Match the clean builder's content-derived archive ID, so different font
    # payloads cannot alias the same serialized file name inside Unity.
    serialized = fonts[0].assets_file
    for obj in environment.objects:
        if obj.type.name not in ('AssetBundle', 'Material', 'Texture2D'):
            continue
        metadata = obj.read_typetree()
        if obj.type.name == 'AssetBundle':
            if bundle_name:
                if Path(bundle_name).name != bundle_name or not bundle_name.endswith('.bundle'):
                    raise ValueError('Invalid Unity bundle name')
                metadata['m_Name'] = metadata['m_AssetBundleName'] = bundle_name
            metadata['m_Container'] = [(font_name.lower(), entry) for _, entry in metadata['m_Container']]
        else:
            metadata['m_Name'] = font_name + (' Material' if obj.type.name == 'Material' else ' Texture')
        obj.save_typetree(metadata)
    cab_name = 'CAB-' + hashlib.sha256(replacement + font_name.encode()).hexdigest()[:32]
    serialized.name = cab_name
    environment.file.files = {cab_name: serialized}
    data = environment.file.save(packer='lz4')
    return data, verify(data, replacement, font_name=font_name)


def prepare(template, output, *, cache=None, font_path=None, config=None):
    if template.resolve() == output.resolve():
        raise ValueError('Output must not overwrite the template')
    spec = json.loads((config or ROOT / 'config/font-source.json').read_text())
    replacement = resolve_font(spec, cache or ROOT / '.analysis/fonts', font_path)
    identity = spec.get('bundle', {})
    data, report = replace_bundle_font(template.read_bytes(), replacement,
                                      font_name=identity.get('font_name'), bundle_name=identity.get('name'))
    atomic_write(output, data)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('template', type=Path, help='Existing standalone module font bundle')
    parser.add_argument('output', type=Path)
    parser.add_argument('--font', type=Path, help='Use the pinned font from a local file, without downloading')
    parser.add_argument('--cache', type=Path)
    parser.add_argument('--config', type=Path, help='Alternate pinned font specification')
    args = parser.parse_args()
    print(json.dumps(prepare(args.template, args.output, cache=args.cache, font_path=args.font,
                             config=args.config), indent=2))


if __name__ == '__main__':
    main()
