"""Reject diagnostic payloads and verify the selected font before publication."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from zipfile import ZipFile

from build_text_update import validate_json
from check_runtime_images import read_directory, validate as validate_images

ROOT = Path(__file__).resolve().parents[1]
ASSETS = 'assets/idoly-localify/'
PAYLOADS = {'dict.json', 'web-notice.json', 'card-stories.json',
            'event-stories.json', 'love-stories.json', 'resource-han-rounded.bundle',
            'love-rounded.bundle'}
# Removed ad-hoc probes must not return to a distributed native library.
DIAGNOSTIC_MARKERS = (b'HomeTalk glyph diagnostic:', b'Message picker geometry:',
                      b'Constrained title route:', b'Constrained title geometry after:',
                      b'League glyph lookup font route:', b'TMP start label font route:',
                      b'Shop badge: game IsMarketNotify=', b'Activity submesh rect:')


def check(apk, project=ROOT):
    import UnityPy
    from build_font_bundle import verify
    from prepare_module_font import verify_font

    images = read_directory(project / 'app/src/main/assets/idoly-localify/images')
    with ZipFile(apk) as archive:
        names = archive.namelist()
        payloads = {name[len(ASSETS):] for name in names if name.startswith(ASSETS)}
        expected = PAYLOADS | {'images/' + name for name in images}
        if payloads != expected or len(names) != len(set(names)):
            raise ValueError('Unexpected or missing module assets')
        embedded_images = {name: archive.read(ASSETS + 'images/' + name) for name in images}
        validate_images(embedded_images)
        if embedded_images != images:
            raise ValueError('Module images differ from the reviewed source payload')
        for name in ('dict.json', 'web-notice.json'):
            validate_json(name, archive.read(ASSETS + name))
        for name in names:
            if name.startswith('assets/') and any(
                    part in {'test', 'tests', 'capture', '.analysis', 'secrets'}
                    for part in Path(name).parts):
                raise ValueError('Local diagnostic data is present in module assets')
            if name.startswith('lib/') and name.endswith('.so'):
                binary = archive.read(name)
                if any(marker in binary for marker in DIAGNOSTIC_MARKERS):
                    raise ValueError('Ad-hoc diagnostic probe remains in a native library')
        reports = {}
        for bundle_name, config_name in (
                ('resource-han-rounded.bundle', 'font-source.json'),
                ('love-rounded.bundle', 'love-font-source.json')):
            bundle = archive.read(ASSETS + bundle_name)
            environment = UnityPy.load(bundle)
            fonts = [obj for obj in environment.objects if obj.type.name == 'Font']
            if len(fonts) != 1:
                raise ValueError('Expected one standalone replacement font per bundle')
            font = bytes(fonts[0].read_typetree()['m_FontData'])
            spec = json.loads((project / 'config' / config_name).read_text())
            verify_font(font, spec)
            identity = spec.get('bundle', {})
            options = {'font_name': identity['font_name']} if 'font_name' in identity else {}
            report = verify(bundle, font, **options)
            reports[bundle_name] = {'sha256': report['font_sha256'], 'weight': spec['weight']}
    main_font = reports['resource-han-rounded.bundle']
    return {'font_sha256': main_font['sha256'], 'font_weight': main_font['weight'],
            'fonts': reports,
            'automatic_capture': False, 'payload_files': len(payloads),
            'runtime_atlases': len(images) - 1}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--project', type=Path, default=ROOT)
    args = parser.parse_args()
    print(json.dumps(check(args.apk, args.project), ensure_ascii=False))
