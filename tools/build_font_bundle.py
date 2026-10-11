#!/usr/bin/env python3
"""Build a local, independent Unity font bundle without modifying a game APK.

The APK supplies version-compatible Font/Material/empty Texture2D schemas. The
UnityFS template supplies only the AssetBundle schema and archive format. All
original objects, resource streams and game dependencies are discarded.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import zipfile

import UnityPy
from patch_font import FONT_NAME, INDEPENDENT_FONT_NAME, font_bytes, replacement_family, split_names

ASSET_KEY = INDEPENDENT_FONT_NAME.lower()
BUNDLE_NAME = 'resource-han-rounded.bundle'


def ptr(path_id):
    return {'m_FileID': 0, 'm_PathID': path_id}


def pointers(value):
    if isinstance(value, dict):
        if set(value) == {'m_FileID', 'm_PathID'}:
            yield value
        else:
            for item in value.values():
                yield from pointers(item)
    elif isinstance(value, (list, tuple)):
        for item in value:
            yield from pointers(item)


def verify(data, replacement, *, font_name=INDEPENDENT_FONT_NAME):
    env = UnityPy.load(data)
    if len(env.file.files) != 1:
        raise ValueError('Bundle must contain exactly one serialized file, no resource streams')
    objects = list(env.objects)
    if sorted(o.type.name for o in objects) != ['AssetBundle', 'Font', 'Material', 'Texture2D']:
        raise ValueError('Unexpected objects in standalone bundle')
    trees = {o.type.name: o.read_typetree() for o in objects}
    font = trees['Font']
    if font['m_Name'] != font_name or bytes(font['m_FontData']) != replacement:
        raise ValueError('Embedded font does not match requested font byte-for-byte')
    if font['m_FontNames'] != [replacement_family(replacement)] or font['m_FallbackFonts']:
        raise ValueError('Font family/fallback mismatch')
    texture = trees['Texture2D']
    if any(texture[k] for k in ('m_Width', 'm_Height', 'm_CompleteImageSize', 'image data', 'm_PlatformBlob')) or any(texture['m_StreamData'].values()):
        raise ValueError('Texture unexpectedly contains image payload')
    sf = objects[0].assets_file
    if len(sf.externals) != 1 or sf.externals[0].path != 'Library/unity default resources':
        raise ValueError('Unexpected external dependency')
    ids = {o.path_id for o in objects}
    external_pointers = []
    for obj in objects:
        tree = trees[obj.type.name]
        # Font data is binary, so do not walk millions of individual bytes.
        for reference in pointers({k: v for k, v in tree.items() if k != 'm_FontData'}):
            if reference['m_FileID']:
                external_pointers.append(reference)
            elif reference['m_PathID'] and reference['m_PathID'] not in ids:
                raise ValueError('Dangling local reference')
    if external_pointers != [{'m_FileID': 1, 'm_PathID': 10101}]:
        raise ValueError('Only the built-in font shader may be external')
    if font['m_DefaultMaterial'] != ptr(3) or font['m_Texture'] != ptr(4):
        raise ValueError('Unexpected Font dependencies')
    if dict(trees['Material']['m_SavedProperties']['m_TexEnvs'])['_MainTex']['m_Texture'] != ptr(4):
        raise ValueError('Material/texture reference mismatch')
    bundle = trees['AssetBundle']
    if bundle['m_Container'] != [(font_name.lower(), {'preloadIndex': 0, 'preloadSize': 3, 'asset': ptr(2)})] or bundle['m_Dependencies']:
        raise ValueError('Unexpected bundle container/dependencies')
    if bundle['m_PreloadTable'] != [ptr(2), ptr(3), ptr(4)]:
        raise ValueError('Unexpected preload table')
    if FONT_NAME.encode() in sf.save():
        raise ValueError('Original font name remains in output')
    return {'font_name': font_name, 'asset_key': font_name.lower(),
            'unity_version': sf.unity_version, 'font_bytes': len(replacement),
            'font_sha256': hashlib.sha256(replacement).hexdigest(),
            'bundle_bytes': len(data), 'bundle_sha256': hashlib.sha256(data).hexdigest(),
            'objects': [{'path_id': o.path_id, 'type': o.type.name} for o in objects],
            'external_dependencies': [sf.externals[0].path],
            'verified': ['exact font bytes', 'only four allowed objects', 'empty texture',
                         'all references valid', 'no original font or image/resource streams']}


def build(base_apk, cjk_font, template_path, output, face=None):
    replacement = font_bytes(cjk_font, face)
    with zipfile.ZipFile(base_apk) as apk:
        source = UnityPy.load(b''.join(apk.read(n) for n in split_names(apk)))
    matches = [o for o in source.objects if o.type.name == 'Font' and o.read().m_Name == FONT_NAME]
    if len(matches) != 1:
        raise ValueError('Expected exactly one original dynamic font')
    font_obj = matches[0]
    sf = font_obj.assets_file
    font = font_obj.read_typetree()
    def dependency(field, expected):
        p = font[field]
        obj = sf.objects.get(p['m_PathID']) if p['m_FileID'] == 0 else None
        if obj is None or obj.type.name != expected:
            raise ValueError('Unexpected source font dependency: ' + field)
        return obj
    material_obj = dependency('m_DefaultMaterial', 'Material')
    texture_obj = dependency('m_Texture', 'Texture2D')
    material = material_obj.read_typetree()
    texture = texture_obj.read_typetree()
    shader = material['m_Shader']
    if shader['m_PathID'] != 10101 or not shader['m_FileID'] or sf.externals[shader['m_FileID'] - 1].path != 'Library/unity default resources':
        raise ValueError('Expected the built-in Unity font shader')
    builtin = copy.copy(sf.externals[shader['m_FileID'] - 1])
    UnityPy.config.FALLBACK_UNITY_VERSION = sf.unity_version
    template = UnityPy.load(str(template_path))
    bundles = [o for o in template.objects if o.type.name == 'AssetBundle']
    if len(bundles) != 1 or bundles[0].assets_file.header.version != sf.header.version:
        raise ValueError('Need one AssetBundle template with matching serialized file format')
    bundle_obj = bundles[0]
    metadata = bundle_obj.read_typetree()
    metadata.update(m_Name=BUNDLE_NAME, m_AssetBundleName=BUNDLE_NAME,
                    m_PreloadTable=[ptr(2), ptr(3), ptr(4)],
                    m_Container=[(ASSET_KEY, {'preloadIndex': 0, 'preloadSize': 3, 'asset': ptr(2)})],
                    m_MainAsset={'preloadIndex': 0, 'preloadSize': 0, 'asset': ptr(0)},
                    m_Dependencies=[], m_IsStreamedSceneAssetBundle=False, m_SceneHashes=[])
    font.update(m_Name=INDEPENDENT_FONT_NAME, m_FontData=list(replacement),
                m_FontNames=[replacement_family(replacement)], m_FallbackFonts=[],
                m_DefaultMaterial=ptr(3), m_Texture=ptr(4))
    material['m_Name'] = INDEPENDENT_FONT_NAME + ' Material'
    material['m_Shader'] = {'m_FileID': 1, 'm_PathID': 10101}
    for name, slot in material['m_SavedProperties']['m_TexEnvs']:
        if name != '_MainTex':
            raise ValueError('Unexpected material texture slot')
        slot['m_Texture'] = ptr(4)
    texture['m_Name'] = INDEPENDENT_FONT_NAME + ' Texture'
    originals = [bundle_obj, font_obj, material_obj, texture_obj]
    trees = [metadata, font, material, texture]
    # Resolve schemas before replacing the source type table.
    types = []
    for obj in originals:
        t = copy.copy(obj.serialized_type)
        t.node = obj._get_typetree_node()
        t.type_dependencies = ()
        types.append(t)
    sf.types = types
    sf._enable_type_tree = False
    sf.objects = {}
    sf.externals = [builtin]
    sf.script_types = []
    sf.ref_types = []
    sf.userInformation = ''
    sf.flags = 4
    for index, (obj, tree) in enumerate(zip(originals, trees)):
        clone = copy.copy(obj)
        clone.assets_file = sf
        clone.path_id = index + 1
        clone.type_id = index
        clone.serialized_type = types[index]
        clone.save_typetree(tree)
        sf.objects[clone.path_id] = clone
    archive = template.file
    archive.version_engine = sf.unity_version
    archive.version_player = '5.x.x'
    cab_name = 'CAB-' + hashlib.sha256(replacement + INDEPENDENT_FONT_NAME.encode()).hexdigest()[:32]
    sf.name = cab_name
    sf.parent = archive
    archive.files = {cab_name: sf}
    data = archive.save(packer='lz4')
    report = verify(data, replacement)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    output.with_suffix(output.suffix + '.verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base_apk', type=Path)
    parser.add_argument('cjk_font', type=Path)
    parser.add_argument('bundle_template', type=Path)
    parser.add_argument('output_bundle', type=Path)
    parser.add_argument('--face', type=int)
    args = parser.parse_args()
    inputs = (args.base_apk, args.cjk_font, args.bundle_template)
    if args.output_bundle.resolve() in [p.resolve() for p in inputs]:
        parser.error('Output must not overwrite any input')
    build(*inputs, args.output_bundle, args.face)


if __name__ == '__main__':
    main()
