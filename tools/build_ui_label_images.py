#!/usr/bin/env python3
"""Re-letter reviewed UI labels using their verified Image rendering geometry."""
from __future__ import annotations

import argparse
from copy import deepcopy
from hashlib import sha256
import json
from pathlib import Path
import shutil
import statistics
import struct
from zipfile import ZipFile

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont
import UnityPy

from patch_images import check_regions, load_manifest, refresh_sprite_padding, sprite_padding_region

ROOT = Path(__file__).resolve().parents[1]


def digest(data: bytes) -> str:
    return sha256(data).hexdigest()


def fill_enclosed_transparency(image: Image.Image, color: list[int], gap_radius: int = 0) -> Image.Image:
    """Back enclosed glyph counters without painting the exterior background."""
    if len(color) != 4 or color[3] != 255:
        raise ValueError('Enclosed label background must be opaque RGBA')
    # Work before downsampling, where the drawn strokes have hard coverage.
    # A transparent border ensures even edge-touching strokes are floodable.
    width, height = image.size
    occupied = image.getchannel('A').point(lambda value: 255 if value else 0)
    if not isinstance(gap_radius, int) or gap_radius < 0:
        raise ValueError('Label gap radius must be a nonnegative integer')
    if gap_radius:
        # Close narrow cracks in a continuous outline without expanding its
        # convex outer boundary. The glyph fill is drawn separately above it.
        kernel = gap_radius * 2 + 1
        closed = occupied.filter(ImageFilter.MaxFilter(kernel)).filter(ImageFilter.MinFilter(kernel))
    else:
        closed = occupied
    outside = Image.new('L', (width + 2, height + 2))
    outside.paste(closed, (1, 1))
    ImageDraw.floodfill(outside, (0, 0), 128)
    holes = outside.crop((1, 1, width + 1, height + 1)).point(lambda value: 255 if value == 0 else 0)
    # Back the interior, including its partially transparent antialiased pixels.
    # Filling only alpha-zero pixels leaves a grey fringe inside a white counter.
    # Keep one supersampled edge pixel so the exterior retains its antialiasing.
    interior = ImageChops.lighter(closed, holes).filter(ImageFilter.MinFilter(3))
    result = Image.new('RGBA', image.size, tuple(color))
    result.putalpha(interior)
    result.alpha_composite(image)
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


def restore_sampled_label_background(original: Image.Image, box: list[int], columns: list[int]) -> Image.Image:
    """Restore a row gradient inside the original bubble's opaque interior."""
    if not columns or any(not 0 <= x < original.width for x in columns):
        raise ValueError('Invalid background sample columns')
    result = original.crop(box)
    draw = ImageDraw.Draw(result)
    for y in range(box[1], box[3]):
        samples = [original.getpixel((x, y)) for x in columns]
        color = tuple(round(statistics.median(px[c] for px in samples)) for c in range(4))
        if color[3] != 255:
            raise ValueError('Background sample reaches the transparent border')
        positions = [x for x in range(original.width)
                     if max(abs(a - b) for a, b in zip(original.getpixel((x, y)), color)) <= 3]
        if not positions:
            raise ValueError('Background samples do not identify an interior')
        left, right = max(box[0], min(positions)), min(box[2] - 1, max(positions))
        if left <= right:
            draw.line((left - box[0], y - box[1], right - box[0], y - box[1]), fill=color)
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
    text_box = item.get('text_box', [0, 0, width, height])
    ellipse = item.get('background_ellipse')
    for name, box in [('text_box', text_box), ('background_ellipse', ellipse)]:
        if box is not None and (len(box) != 4 or any(type(v) is not int for v in box) or
                                not 0 <= box[0] < box[2] <= width or
                                not 0 <= box[1] < box[3] <= height):
            raise ValueError(f'{name} exceeds label canvas')
    layout_width, layout_height = text_box[2] - text_box[0], text_box[3] - text_box[1]
    margin = item['mesh_margin']
    safe = mask.filter(ImageFilter.MinFilter(margin * 2 + 1)) if margin else mask
    # Also reserve a physical texture border for bilinear sampling.
    border = Image.new('L', size)
    ImageDraw.Draw(border).rectangle((margin, margin, width - margin - 1, height - margin - 1), fill=255)
    safe = ImageChops.multiply(safe, border)
    for font_size in range(item['font_size'], item['min_font_size'] - 1, -1):
        font = ImageFont.truetype(str(font_path), font_size * scale)
        # Render beyond the destination first, so overflow cannot be mistaken
        # for a successful fit after the image canvas has clipped the strokes.
        padding = 8 * scale
        layer = Image.new('RGBA', (width * scale + padding * 2, height * scale + padding * 2))
        draw = ImageDraw.Draw(layer)
        if ellipse is not None:
            draw.ellipse((ellipse[0] * scale + padding, ellipse[1] * scale + padding,
                          ellipse[2] * scale + padding - 1, ellipse[3] * scale + padding - 1),
                         fill=tuple(fill))
        strokes = item.get('strokes', [
            {'width': item.get('white_stroke', 0) + item.get('outer_stroke', 0), 'color': fill},
            {'width': item.get('white_stroke', 0), 'color': white}])
        text = item['text']
        spacing = item.get('line_spacing', 4 / scale) * scale
        if item['orientation'] == 'horizontal':
            lines = text.split('\n')
            boxes = [draw.textbbox((0, 0), line, font=font) for line in lines]
            total = sum(b[3] - b[1] for b in boxes) + spacing * (len(lines) - 1)
            top = (layout_height * scale - total) / 2
            placements = []
            for line, bbox in zip(lines, boxes):
                placements.append((line, (layout_width * scale - bbox[2] + bbox[0]) / 2 - bbox[0], top - bbox[1]))
                top += bbox[3] - bbox[1] + spacing
        elif item['orientation'] == 'vertical':
            boxes = [draw.textbbox((0, 0), char, font=font) for char in text]
            gap = item['glyph_gap'] * scale
            total = sum(b[3] - b[1] for b in boxes) + gap * (len(boxes) - 1)
            top = (layout_height * scale - total) / 2
            placements = []
            for char, bbox in zip(text, boxes):
                placements.append((char, (layout_width * scale - bbox[2] + bbox[0]) / 2 - bbox[0], top - bbox[1]))
                top += bbox[3] - bbox[1] + gap
        else:
            raise ValueError('Unknown label orientation')
        adjusted = []
        for text, x, y in placements:
            if item.get('align') == 'left':
                bbox = draw.textbbox((0, 0), text, font=font)
                x = (max((stroke['width'] for stroke in strokes), default=0) + 3) * scale - bbox[0]
            adjusted.append((text, x + text_box[0] * scale + padding, y + text_box[1] * scale + padding))
        if item.get('closed_outline'):
            # Render each outline for the whole label before adding the next
            # layer. Adjacent characters share a continuous sticker silhouette;
            # the original glyph counters remain visible against its white core.
            for stroke in strokes:
                outline = Image.new('RGBA', layer.size)
                outline_draw = ImageDraw.Draw(outline)
                for text, x, y in adjusted:
                    outline_draw.text((x, y), text, font=font, fill=tuple(stroke['color']),
                                      stroke_width=round(stroke['width'] * scale),
                                      stroke_fill=tuple(stroke['color']))
                layer.alpha_composite(fill_enclosed_transparency(outline, stroke['color']))
            draw = ImageDraw.Draw(layer)
        for text, x, y in adjusted:
            for stroke in ([] if item.get('closed_outline') else strokes):
                draw.text((x, y), text, font=font, fill=tuple(fill),
                          stroke_width=round(stroke['width'] * scale), stroke_fill=tuple(stroke['color']),
                          spacing=spacing, align='center')
            draw.text((x, y), text, font=font, fill=tuple(fill), spacing=spacing, align='center')
        if 'enclosed_background_rgba' in item:
            backed = fill_enclosed_transparency(layer, item['enclosed_background_rgba'],
                                               round(item.get('outline_gap_fill', 0) * scale))
            if 'enclosed_background_boxes' in item:
                for box in item['enclosed_background_boxes']:
                    if (len(box) != 4 or any(type(value) is not int for value in box)
                            or not 0 <= box[0] < box[2] <= width
                            or not 0 <= box[1] < box[3] <= height):
                        raise ValueError('Enclosed label background exceeds its canvas')
                    scaled_box = tuple(value * scale + padding for value in box)
                    layer.paste(backed.crop(scaled_box), scaled_box)
            else:
                layer = backed
        expanded = layer.resize((width + 16, height + 16), Image.Resampling.LANCZOS)
        expanded_alpha = expanded.getchannel('A')
        cutoff = item.get('alpha_cutoff', 0)
        significant = expanded_alpha.point(lambda value: 255 if value > cutoff else 0)
        outside = Image.new('L', expanded.size, 255)
        outside.paste(0, (8, 8, width + 8, height + 8))
        if ImageChops.multiply(significant, outside).getbbox():
            continue
        image = expanded.crop((8, 8, width + 8, height + 8))
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


def validate_rectangle_image(raw: bytes, script_pointer=(1, 9016)) -> None:
    # Unity 2020 UI.Image: PPtr sprite@88, type@100, UseSpriteMesh@128.
    # Consumer prefab SHA and Image MonoScript PPtr bind this serialized layout.
    if len(raw) != 136 or struct.unpack_from('<iq', raw, 16) != tuple(script_pointer):
        raise ValueError('Unreviewed UI.Image serialized layout')
    if raw[128] != 0:
        raise ValueError('UI.Image uses sprite mesh; rectangle rendering is unsafe')


def build(base: Path, config_path: Path, font: Path, output: Path, merge_manifest: Path,
          label_fonts: dict[str, Path] | None = None) -> Path:
    config = json.loads(config_path.read_text(encoding='utf-8'))
    if config['schema_version'] != 1 or digest(font.read_bytes()) != config['font_sha256']:
        raise ValueError('Unsupported config or unexpected label font')
    label_fonts = label_fonts or {}
    for name, spec in config.get('fonts', {}).items():
        if name not in label_fonts or digest(label_fonts[name].read_bytes()) != spec['sha256']:
            raise ValueError('Missing or unexpected label font: ' + name)
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
                    validate_rectangle_image(obj.get_raw_data(), consumer.get('script_pointer', (1, 9016)))
                mask = Image.new('L', before.size, 255)
            elif mode == 'sprite_mesh':
                mask = mesh_mask(sprite, packed, box, original_image.height)
            else:
                raise ValueError('Unknown label render mode')
            fill = item.get('fill_rgba', config['fill_rgba'])
            selected_font = label_fonts[item['font']] if item.get('font') else font
            patch, report = render_label((local[2] - local[0], local[3] - local[1]), item, selected_font,
                                         fill, config['white_rgba'], mask.crop(local))
            after = before.copy()
            if 'background_sample_columns' in item:
                # Reviewed columns beside the lettering retain the bubble's
                # vertical gradient. Leave its border and pointer pixel-exact.
                background = restore_sampled_label_background(before, local, item['background_sample_columns'])
                background.alpha_composite(patch)
                after.paste(background, (local[0], local[1]))
            elif 'background_rgba' in item:
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
    if label_fonts:
        merged['ui_label_fonts'] = {name: digest(path.read_bytes()) for name, path in label_fonts.items()}
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
    parser.add_argument('--label-font', action='append', default=[], metavar='NAME=PATH')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--merge-manifest', type=Path, required=True)
    args = parser.parse_args()
    overrides = dict(item.split('=', 1) for item in args.label_font)
    print(build(args.base, args.config, args.font, args.output_dir, args.merge_manifest,
                {name: Path(path) for name, path in overrides.items()}))


if __name__ == '__main__':
    main()
