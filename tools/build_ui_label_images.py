#!/usr/bin/env python3
"""Re-letter reviewed UI labels using their verified Image rendering geometry."""
from __future__ import annotations

import argparse
from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
import shutil
import struct
from zipfile import ZipFile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
import UnityPy

from patch_images import check_regions, load_manifest, refresh_sprite_padding, sprite_padding_region

ROOT = Path(__file__).resolve().parents[1]


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def fill_enclosed_transparency(image: Image.Image, color: list[int]) -> Image.Image:
    """Back enclosed glyph counters without painting the exterior background."""
    if len(color) != 4 or color[3] != 255:
        raise ValueError('Enclosed label background must be opaque RGBA')
    # Work before downsampling, where the drawn strokes have hard coverage.
    # A transparent border ensures even edge-touching strokes are floodable.
    width, height = image.size
    occupied = image.getchannel('A').point(lambda value: 255 if value else 0)
    outside = Image.new('L', (width + 2, height + 2))
    outside.paste(occupied, (1, 1))
    ImageDraw.floodfill(outside, (0, 0), 128)
    holes = outside.crop((1, 1, width + 1, height + 1)).point(lambda value: 255 if value == 0 else 0)
    result = image.copy()
    result.paste(tuple(color), (0, 0, width, height), holes)
    return result


def restore_solid_label_background(original: Image.Image, color: list[int], tolerance: int) -> Image.Image:
    """Restore reviewed flat pills between their pristine scanline edges.

    Matching background pixels delimit each row; the antialiased outer edge,
    original alpha and corner pixels remain untouched. Source hashes and the
    configured tolerance bind this operation to the inspected static sprites.
    """
    if len(color) != 4 or color[3] != 255 or not 0 <= tolerance <= 8:
        raise ValueError('Unreviewed solid label background')
    result = original.copy()
    for y in range(original.height):
        positions = [x for x in range(original.width)
                     if max(abs(a - b) for a, b in zip(original.getpixel((x, y)), color)) <= tolerance]
        if positions:
            for x in range(positions[0], positions[-1] + 1):
                result.putpixel((x, y), tuple(color))
    return result


def mesh_mask(sprite: dict, packed: dict, box: list[int], texture_height: int) -> Image.Image:
    vertex = sprite['m_RD']['m_VertexData']
    position = vertex['m_Channels'][0]
    if position != {'stream': 0, 'offset': 0, 'format': 0, 'dimension': 3}:
        raise ValueError('Unreviewed sprite position layout')
    if any(channel['stream'] == 0 and channel['dimension'] for channel in vertex['m_Channels'][1:]):
        raise ValueError('Unreviewed sprite position stride')
    raw = vertex['m_DataSize']
    uv = packed['uvTransform']
    points = []
    for index in range(vertex['m_VertexCount']):
        x, y, _ = struct.unpack_from('<fff', raw, index * 12)
        points.append((x * uv['x'] + uv['y'] - box[0],
                       texture_height - (y * uv['z'] + uv['w']) - box[1]))
    indices = bytes(sprite['m_RD']['m_IndexBuffer'])
    mask = Image.new('L', (box[2] - box[0], box[3] - box[1]))
    draw = ImageDraw.Draw(mask)
    for submesh in sprite['m_RD']['m_SubMeshes']:
        if submesh['topology'] != 0 or submesh['indexCount'] % 3:
            raise ValueError('Only reviewed triangular sprite meshes are supported')
        start = submesh['firstByte']
        for offset in range(start, start + submesh['indexCount'] * 2, 6):
            triangle = struct.unpack_from('<HHH', indices, offset)
            draw.polygon([points[i + submesh['baseVertex']] for i in triangle], fill=255)
    if not mask.getbbox():
        raise ValueError('Empty sprite mesh')
    return mask


def render_label(size: tuple[int, int], item: dict, font_path: Path,
                 fill: list[int], white: list[int], mask: Image.Image) -> tuple[Image.Image, dict]:
    if not item.get('text'):
        raise ValueError('Image labels require nonempty text')
    scale = 4
    width, height = size
    margin = item['mesh_margin']
    safe = mask.filter(ImageFilter.MinFilter(margin * 2 + 1)) if margin else mask
    # Also reserve a physical texture border for bilinear sampling.
    border = Image.new('L', size)
    ImageDraw.Draw(border).rectangle((margin, margin, width - margin - 1, height - margin - 1), fill=255)
    safe = ImageChops.multiply(safe, border)
    for font_size in range(item['font_size'], item['min_font_size'] - 1, -1):
        font = ImageFont.truetype(str(font_path), font_size * scale)
        layer = Image.new('RGBA', (width * scale, height * scale))
        draw = ImageDraw.Draw(layer)
        strokes = item.get('strokes', [
            {'width': item.get('white_stroke', 0) + item.get('outer_stroke', 0), 'color': fill},
            {'width': item.get('white_stroke', 0), 'color': white}])
        text = item['text']
        spacing = item.get('line_spacing', 4 / scale) * scale
        if item['orientation'] == 'horizontal':
            lines = text.split('\n')
            boxes = [draw.textbbox((0, 0), line, font=font) for line in lines]
            total = sum(b[3] - b[1] for b in boxes) + spacing * (len(lines) - 1)
            top = (height * scale - total) / 2
            placements = []
            for line, bbox in zip(lines, boxes):
                placements.append((line, (width * scale - bbox[2] + bbox[0]) / 2 - bbox[0], top - bbox[1]))
                top += bbox[3] - bbox[1] + spacing
        elif item['orientation'] == 'vertical':
            boxes = [draw.textbbox((0, 0), char, font=font) for char in text]
            gap = item['glyph_gap'] * scale
            total = sum(b[3] - b[1] for b in boxes) + gap * (len(boxes) - 1)
            top = (height * scale - total) / 2
            placements = []
            for char, bbox in zip(text, boxes):
                placements.append((char, (width * scale - bbox[2] + bbox[0]) / 2 - bbox[0], top - bbox[1]))
                top += bbox[3] - bbox[1] + gap
        else:
            raise ValueError('Unknown label orientation')
        for text, x, y in placements:
            if item.get('align') == 'left':
                bbox = draw.textbbox((0, 0), text, font=font)
                x = (max((stroke['width'] for stroke in strokes), default=0) + 3) * scale - bbox[0]
            for stroke in strokes:
                draw.text((x, y), text, font=font, fill=tuple(fill),
                          stroke_width=round(stroke['width'] * scale), stroke_fill=tuple(stroke['color']),
                          spacing=spacing, align='center')
            draw.text((x, y), text, font=font, fill=tuple(fill), spacing=spacing, align='center')
        if 'enclosed_background_rgba' in item:
            layer = fill_enclosed_transparency(layer, item['enclosed_background_rgba'])
        image = layer.resize(size, Image.Resampling.LANCZOS)
        alpha = image.getchannel('A')
        if item.get('alpha_cutoff'):
            # Drop only the low-opacity Lanczos ringing around new glyphs.
            # This is fixed in the reviewed config, never used to clip to a mesh.
            alpha = alpha.point(lambda value: value if value > item['alpha_cutoff'] else 0)
            image.putalpha(alpha)
        if alpha.getbbox() and not ImageChops.multiply(alpha, ImageChops.invert(safe)).getbbox():
            # Retain the original style of pink transparent RGB to avoid dark
            # interpolation fringes; opacity outside the letters stays zero.
            pixels = image.load()
            for y in range(height):
                for x in range(width):
                    if pixels[x, y][3] == 0:
                        pixels[x, y] = (*fill[:3], 0)
            return image, {'font_size': font_size, 'alpha_bbox': list(alpha.getbbox()),
                           'safe_margin_pixels': margin, 'alpha_outside_safe_region': 0,
                           'render_mode': item.get('render_mode', 'sprite_mesh'),
                           'canvas_size': list(size)}
    raise ValueError(f"Label cannot fit reviewed safe region: {item['sprite']}")


def validate_rectangle_image(raw: bytes) -> None:
    # Unity 2020 UI.Image: PPtr sprite@88, type@100, UseSpriteMesh@128.
    # Consumer prefab SHA and Image MonoScript PPtr bind this serialized layout.
    if len(raw) != 136 or struct.unpack_from('<iq', raw, 16) != (1, 9016):
        raise ValueError('Unreviewed UI.Image serialized layout')
    if raw[128] != 0:
        raise ValueError('UI.Image uses sprite mesh; rectangle rendering is unsafe')


def build(base: Path, config_path: Path, font: Path, output: Path, merge_manifest: Path) -> Path:
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if config['schema_version'] != 1 or digest(font.read_bytes()) != config['font_sha256']:
        raise ValueError('Unsupported config or unexpected label font')
    merged = deepcopy(load_manifest(merge_manifest))
    entry = config['texture_entry']
    if len(merged['assets']) != 1 or merged['assets'][0]['entry'] != entry:
        raise ValueError('Merge manifest must target the same reviewed texture asset')
    specification = merged['assets'][0]
    if specification['source_sha256'] != config['source_sha256']:
        raise ValueError('Merge manifest uses another source version')
    output.mkdir(parents=True, exist_ok=True)
    with ZipFile(base) as apk:
        source = apk.read(entry)
        if digest(source) != config['source_sha256']:
            raise ValueError('APK texture version differs from reviewed config')
        objects = {o.path_id: o for o in UnityPy.load(source).objects}
        originals, edited, regions, atlases = {}, {}, {}, {}
        for target in config['atlases']:
            atlas_bytes = apk.read(target['entry'])
            if digest(atlas_bytes) != target['source_sha256']:
                raise ValueError('Atlas version differs from reviewed config')
            atlas = next(iter(UnityPy.load(atlas_bytes).objects)).read_typetree()
            texture = objects[target['texture_id']].read()
            if texture.m_Name != target['texture_name']:
                raise ValueError('Texture name differs')
            key = target['name']
            atlases[key] = (target, atlas)
            originals[key] = texture.image.convert('RGBA')
            edited[key] = originals[key].copy()
            regions[key] = []
        inherited = []
        for texture in specification['textures']:
            with Image.open(merge_manifest.parent / texture['png']) as image:
                previous = image.convert('RGBA')
            original_texture = objects[texture['path_id']].read()
            if original_texture.m_Name != texture['name']:
                raise ValueError('Inherited texture identity differs')
            check_regions(original_texture.image.convert('RGBA'), previous, texture['edit_regions'])
            target = output / texture['png']
            target.parent.mkdir(parents=True, exist_ok=True)
            if target.resolve() != (merge_manifest.parent / texture['png']).resolve():
                shutil.copyfile(merge_manifest.parent / texture['png'], target)
            inherited.append({'path_id': texture['path_id'], 'png_sha256': texture['png_sha256'],
                              'preserved_exactly': digest(target.read_bytes()) == texture['png_sha256']})
            for key, (target_info, _) in atlases.items():
                if texture['path_id'] == target_info['texture_id']:
                    edited[key] = previous.copy()
                    regions[key] = deepcopy(texture['edit_regions'])
        reports, previews = [], []
        for item in config['labels']:
            target, atlas = atlases[item['atlas']]
            original_image = originals[item['atlas']]
            sprite_bytes = apk.read(item['entry'])
            if digest(sprite_bytes) != item['source_sha256']:
                raise ValueError('Sprite metadata version differs')
            sprite = next(iter(UnityPy.load(sprite_bytes).objects)).read_typetree()
            if sprite['m_Name'] != item['sprite']:
                raise ValueError('Sprite identity differs')
            packed = next(v for k, v in atlas['m_RenderDataMap'] if k == sprite['m_RenderDataKey'])
            if packed['settingsRaw'] != 67 or packed['texture']['m_PathID'] != target['texture_id']:
                raise ValueError('Unreviewed atlas packing or texture')
            rect = packed['textureRect']
            box = [round(rect['x']), original_image.height - round(rect['y'] + rect['height']),
                   round(rect['x'] + rect['width']), original_image.height - round(rect['y'])]
            if box != item['packed_box']:
                raise ValueError('Packed sprite rectangle differs')
            before = original_image.crop(box)
            local = item.get('edit_box', [0, 0, before.width, before.height])
            if not (0 <= local[0] < local[2] <= before.width and 0 <= local[1] < local[3] <= before.height):
                raise ValueError('Label edit exceeds packed rectangle')
            bounds = [box[0] + local[0], box[1] + local[1], box[0] + local[2], box[1] + local[3]]
            edit_region = bounds
            padding = item.get('clear_padding', 0)
            if padding:
                edit_region = sprite_padding_region(box, padding, atlas, sprite['m_RenderDataKey'],
                                                     target['texture_id'], original_image.size,
                                                     edit_box=bounds)
            if any(edit_region[0] < r[2] and r[0] < edit_region[2]
                   and edit_region[1] < r[3] and r[1] < edit_region[3]
                   for r in regions[item['atlas']]):
                raise ValueError('New label overlaps an existing reviewed image edit')
            mode = item.get('render_mode', 'sprite_mesh')
            if mode == 'image_rectangle':
                if not item.get('image_consumers'):
                    raise ValueError('Rectangle rendering requires reviewed UI.Image consumers')
                for consumer in item['image_consumers']:
                    data = apk.read(consumer['entry'])
                    if digest(data) != consumer['source_sha256']:
                        raise ValueError('UI.Image consumer version differs')
                    obj = next(o for o in UnityPy.load(data).objects if o.path_id == consumer['image_path_id'])
                    validate_rectangle_image(obj.get_raw_data())
                mask = Image.new('L', before.size, 255)
            elif mode == 'sprite_mesh':
                mask = mesh_mask(sprite, packed, box, original_image.height)
            else:
                raise ValueError('Unknown label render mode')
            fill = item.get('fill_rgba', config['fill_rgba'])
            patch, report = render_label((local[2] - local[0], local[3] - local[1]), item, font,
                                         fill, config['white_rgba'], mask.crop(local))
            after = before.copy()
            if 'background_rgba' in item:
                background = restore_solid_label_background(before.crop(local), item['background_rgba'],
                                                            item['background_tolerance'])
                background.alpha_composite(patch)
                after.paste(background, (local[0], local[1]))
            else:
                after.paste(patch, (local[0], local[1]))
            check_regions(before, after, [local])
            # The atlas must receive the same composited pixels as the preview,
            # including any restored pill background. Preserve the untouched
            # portion of a partial sprite edit.
            edited[item['atlas']].paste(after.crop(local), (bounds[0], bounds[1]))
            if padding:
                refresh_sprite_padding(edited[item['atlas']], box, edit_region)
            regions[item['atlas']].append(edit_region)
            before.save(output / (item['sprite'] + '-original.png'))
            after.save(output / (item['sprite'] + '-zh-Hans.png'))
            reports.append({'sprite': item['sprite'], 'packed_box': box, 'edit_box': local,
                            'clear_padding': padding, 'atlas_edit_region': edit_region,
                            'unchanged_outside_local_edit': True,
                            'sprite_metadata_sha256': digest(sprite_bytes), **report})
            previews.append((item['sprite'], before, after))
        for key, (target, _) in atlases.items():
            check_regions(originals[key], edited[key], regions[key])
            png = key + '-ui-labels-zh-Hans.png'
            edited[key].save(output / png)
            specification['textures'] = [t for t in specification['textures'] if t['path_id'] != target['texture_id']]
            specification['textures'].append({'path_id': target['texture_id'], 'name': target['texture_name'],
                'png': png, 'png_sha256': digest((output / png).read_bytes()), 'edit_regions': regions[key]})
    merged['ui_label_config_sha256'] = digest(config_path.read_bytes())
    merged['ui_label_font_sha256'] = digest(font.read_bytes())
    merged['merged_from_manifest_sha256'] = digest(merge_manifest.read_bytes())
    (output / 'manifest.json').write_text(json.dumps(merged, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    load_manifest(output / 'manifest.json')
    (output / 'validation.json').write_text(json.dumps({'labels': reports, 'inherited_textures': inherited,
        'pixels_outside_reviewed_regions_unchanged': True, 'sprite_metadata_unchanged': True,
        'apk_written': False}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    row_heights = [max(150, before.height * 3 + 55) for _, before, _ in previews]
    sheet = Image.new('RGB', (1560, sum(row_heights)), '#424654')
    draw = ImageDraw.Draw(sheet)
    top = 0
    for row_height, (name, before, after) in zip(row_heights, previews):
        draw.text((20, top + 8), name + '  ORIGINAL / ZH-HANS', fill='white')
        for x, image in ((60, before), (820, after)):
            enlarged = image.resize((image.width * 3, image.height * 3), Image.Resampling.NEAREST)
            sheet.paste(enlarged, (x, top + 30), enlarged)
        top += row_height
    sheet.save(output / 'comparison.png')
    return output / 'manifest.json'


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--config', type=Path, default=ROOT / 'config/ui-label-images.json')
    parser.add_argument('--font', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--merge-manifest', type=Path, required=True)
    args = parser.parse_args()
    print(build(args.base, args.config, args.font, args.output_dir, args.merge_manifest))


if __name__ == '__main__':
    main()
