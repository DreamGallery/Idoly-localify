#!/usr/bin/env python3
"""Re-letter selected circular UI sprites while preserving their original icons."""

import argparse
from collections import Counter
from hashlib import sha256
import json
import math
from pathlib import Path
from zipfile import ZipFile

from PIL import Image, ImageChops, ImageDraw, ImageFont
import UnityPy

from patch_images import check_regions, refresh_sprite_padding, sprite_padding_region
from build_ui_label_images import fill_enclosed_transparency

ROOT = Path(__file__).resolve().parents[1]


def restore_ticket(image: Image.Image, specification: dict, background: tuple) -> None:
    """Complete the ticket's lower corner before drawing the translated caption."""
    box = specification["box"]
    if (len(box) != 4 or any(type(v) is not int for v in box)
            or not 0 <= box[0] < box[2] <= image.width
            or not 0 <= box[1] < box[3] <= image.height):
        raise ValueError("Invalid ticket repair region")
    if image.crop(tuple(box)).getchannel("A").getextrema() != (255, 255):
        raise ValueError("Ticket repair must stay inside the opaque button background")
    bounds = specification["bounds"]
    radius, stroke = specification["radius"], specification["stroke"]
    if (len(bounds) != 4 or not bounds[0] < bounds[2] or not bounds[1] < bounds[3]
            or not 0 < radius <= min(bounds[2] - bounds[0], bounds[3] - bounds[1]) / 2
            or not 0 <= stroke < min(bounds[2] - bounds[0], bounds[3] - bounds[1]) / 2):
        raise ValueError("Invalid ticket shape")
    scale = 8
    angle = math.radians(specification["angle"])
    cosine, sine = math.cos(angle), math.sin(angle)
    mask = Image.new("L", (image.width * scale, image.height * scale))
    def polygon(painter, points):
        painter.polygon([((cosine * u - sine * v + specification["offset_x"]) * scale,
                          (sine * u + cosine * v) * scale) for u, v in points], fill=255)

    def rounded_rectangle(painter, rect, corner):
        left, top, right, bottom = rect
        points = []
        for x, y, start in ((right - corner, top + corner, -90),
                            (right - corner, bottom - corner, 0),
                            (left + corner, bottom - corner, 90),
                            (left + corner, top + corner, 180)):
            for step in range(17):
                a = math.radians(start + 90 * step / 16)
                u, v = x + corner * math.cos(a), y + corner * math.sin(a)
                points.append((u, v))
        polygon(painter, points)

    rounded_rectangle(ImageDraw.Draw(mask), bounds, radius)
    inner_mask = None
    if stroke:
        inner = [bounds[0] + stroke, bounds[1] + stroke,
                 bounds[2] - stroke, bounds[3] - stroke]
        inner_mask = Image.new("L", mask.size)
        rounded_rectangle(ImageDraw.Draw(inner_mask), inner, max(0.1, radius - stroke))
    mask = mask.resize(image.size, Image.Resampling.LANCZOS).crop(tuple(box))
    patch = Image.new("RGBA", mask.size, (*background, 255))
    patch.paste((255, 255, 255, 255), (0, 0, patch.width, patch.height), mask)
    if inner_mask is not None:
        color = specification["inner_rgb"]
        if len(color) != 3 or any(type(v) is not int or not 0 <= v <= 255 for v in color):
            raise ValueError("Invalid ticket interior colour")
        highlight_mask = None
        if highlight := specification.get("inner_highlight"):
            highlight_color = highlight["rgb"]
            if (len(highlight_color) != 3
                    or any(type(v) is not int or not 0 <= v <= 255 for v in highlight_color)
                    or not inner[0] < highlight["from_u"] < inner[2]):
                raise ValueError("Invalid ticket interior highlight")
            highlight_mask = Image.new("L", inner_mask.size)
            polygon(ImageDraw.Draw(highlight_mask),
                    [(highlight["from_u"], inner[1]), (inner[2], inner[1]),
                     (inner[2], inner[3]), (highlight["from_u"], inner[3])])
            highlight_mask = ImageChops.multiply(highlight_mask, inner_mask)
        inner_mask = inner_mask.resize(image.size, Image.Resampling.LANCZOS).crop(tuple(box))
        patch.paste((*color, 255), (0, 0, patch.width, patch.height), inner_mask)
        if highlight_mask is not None:
            highlight_mask = highlight_mask.resize(image.size, Image.Resampling.LANCZOS).crop(tuple(box))
            patch.paste((*highlight_color, 255), (0, 0, patch.width, patch.height), highlight_mask)
    # Replace the lower corner and the whole straight lower edge together.
    # A horizontal splice through that edge would leave a visible kink.
    edit = Image.new("L", (image.width * scale, image.height * scale))
    painter = ImageDraw.Draw(edit)
    from_y = specification["corner_from_y"]
    if not box[1] <= from_y < box[3]:
        raise ValueError("Invalid ticket corner repair boundary")
    painter.rectangle((box[0] * scale, from_y * scale,
                       box[2] * scale - 1, box[3] * scale - 1), fill=255)
    left, _, right, bottom = bounds
    polygon(painter, [(left - 2, bottom - stroke - 1.5),
                      (right - radius - 1, bottom - stroke - 1.5),
                      (right - radius - 1, bottom + 1.5),
                      (left - 2, bottom + 1.5)])
    edit = edit.resize(image.size, Image.Resampling.LANCZOS).crop(tuple(box))
    image.paste(patch, (box[0], box[1]), edit)


def relabel(original: Image.Image, item: dict, font_path: Path) -> Image.Image:
    width, height = original.size
    cutoff = item.get("erase_from_y", 78)
    circle = item.get("circle_diameter", 100)
    colors = Counter(pixel[:3] for pixel in original.crop((0, 0, width, cutoff)).getdata()
                     if pixel[3] > 240 and min(pixel[:3]) < 100)
    background = colors.most_common(1)[0][0]
    result = original.copy()
    # Restore the circle underneath the old letters. Its top half is pristine:
    # mirror only its alpha, so the white icon itself is never copied or redrawn.
    for y in range(cutoff - 5, height):
        for x in range(width):
            alpha = original.getpixel((x, circle - 1 - y))[3] if y < circle else 0
            # Old coloured text strokes start above the white letters. Remove
            # that fringe outside the circle without touching the icon inside.
            if y >= cutoff or alpha < 255:
                result.putpixel((x, y), (*background, alpha))
    if "ticket_repair" in item:
        restore_ticket(result, item["ticket_repair"], background)
    scale = 4
    layer = Image.new("RGBA", (width * scale, height * scale))
    draw = ImageDraw.Draw(layer)
    font = ImageFont.truetype(str(font_path), item.get("font_size", 24) * scale)
    for line in item["lines"]:
        text, top = line["text"], line["y"]
        box = draw.textbbox((0, 0), text, font=font)
        left = (width * scale - (box[2] - box[0])) / 2 - box[0]
        stroke = item.get("stroke", 3) * scale
        if left + box[0] - stroke < 0 or top * scale - stroke < cutoff * scale:
            raise ValueError(f"Text exceeds its edit region: {item['sprite']}")
        if top * scale + box[3] - box[1] + stroke > height * scale:
            raise ValueError(f"Text exceeds its original canvas: {item['sprite']}")
        draw.text((left, top * scale - box[1]), text, font=font,
                  fill="white", stroke_width=stroke, stroke_fill=(*background, 255))
    layer = fill_enclosed_transparency(layer, [*background, 255])
    layer = layer.resize(original.size, Image.Resampling.LANCZOS)
    result.alpha_composite(layer)
    # Supersampling may produce a one-pixel fringe above the approved band.
    unchanged_top = min(cutoff - 5, item.get("ticket_repair", {}).get("box", [0, height])[1])
    result.paste(original.crop((0, 0, width, unchanged_top)), (0, 0))
    return result


def build(base: Path, config_path: Path, font: Path, output: Path) -> Path:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    if sha256(font.read_bytes()).hexdigest() != config["font_sha256"]:
        raise ValueError("Button font differs from the reviewed configuration")
    output.mkdir(parents=True, exist_ok=True)
    with ZipFile(base) as apk:
        entry = config["texture_entry"]
        source = apk.read(entry)
        if sha256(source).hexdigest() != config["source_sha256"]:
            raise ValueError("Game atlas version differs; review sprite locations before rebuilding")
        environment = UnityPy.load(source)
        objects = {obj.path_id: obj for obj in environment.objects}
        originals, edited, regions, textures = {}, {}, {}, []
        atlas_data = {}
        previews = []
        for target in config["atlases"]:
            atlas = next(iter(UnityPy.load(apk.read(target["entry"])).objects)).read_typetree()
            atlas_data[target["name"]] = atlas
            texture = objects[target["texture_id"]].read()
            originals[target["name"]] = texture.image.convert("RGBA")
            edited[target["name"]] = originals[target["name"]].copy()
            regions[target["name"]] = []
        for item in config["buttons"]:
            sprite = next(iter(UnityPy.load(apk.read(item["entry"])).objects)).read_typetree()
            if sprite["m_Name"] != item["sprite"]:
                raise ValueError("Sprite identity differs")
            atlas_name = item["atlas"]
            atlas = atlas_data[atlas_name]
            packed = next(value for key, value in atlas["m_RenderDataMap"]
                          if key == sprite["m_RenderDataKey"])
            if packed["settingsRaw"] != 67:
                raise ValueError("Only reviewed unrotated rectangular sprites are supported")
            width, height = (int(sprite["m_Rect"][axis]) for axis in ("width", "height"))
            x, bottom = (round(packed["atlasRectOffset"][axis]) for axis in ("x", "y"))
            original = originals[atlas_name]
            y = original.height - bottom - height
            source_sprite = original.crop((x, y, x + width, y + height))
            translated = relabel(source_sprite, item, font)
            # Original trimming can put a neighbour just beyond the logical
            # sprite. Limit writes to the packed rectangle, not its full canvas.
            rect = packed["textureRect"]
            edit_top = min(item.get("erase_from_y", 78) - 5,
                           item.get("ticket_repair", {}).get("box", [0, height])[1])
            bounds = [max(x, round(rect["x"])), y + edit_top,
                      min(x + width, round(rect["x"] + rect["width"])),
                      min(y + height, original.height - round(rect["y"]))]
            local = (bounds[0] - x, bounds[1] - y, bounds[2] - x, bounds[3] - y)
            patch = translated.crop(local)
            # Ensure translated letters are not clipped by the old trim bounds.
            for line in item["lines"]:
                f = ImageFont.truetype(str(font), item.get("font_size", 24))
                box = f.getbbox(line["text"])
                half = (box[2] - box[0]) / 2 + item.get("stroke", 3)
                if width / 2 - half < local[0] or width / 2 + half > local[2]:
                    raise ValueError("Translated text extends beyond packed sprite width")
                if line["y"] + box[3] - box[1] + item.get("stroke", 3) > local[3]:
                    raise ValueError("Translated text extends beyond packed sprite height")
            edited[atlas_name].paste(patch, (bounds[0], bounds[1]))
            # Crop inside the original trim for a truthful before/after preview.
            safe = [round(rect["x"]), original.height - round(rect["y"] + rect["height"]),
                    round(rect["x"] + rect["width"]), original.height - round(rect["y"])]
            edit_region = bounds
            if item.get("clear_padding"):
                edit_region = sprite_padding_region(safe, item["clear_padding"], atlas,
                    sprite["m_RenderDataKey"], packed["texture"]["m_PathID"], original.size,
                    edit_box=bounds)
                refresh_sprite_padding(edited[atlas_name], safe, edit_region)
            if any(edit_region[0] < r[2] and r[0] < edit_region[2]
                   and edit_region[1] < r[3] and r[1] < edit_region[3] for r in regions[atlas_name]):
                raise ValueError("Button padding overlaps another reviewed image edit")
            regions[atlas_name].append(edit_region)
            before = original.crop(safe)
            after = edited[atlas_name].crop(safe)
            before.save(output / (item["sprite"] + "-original.png"))
            after.save(output / (item["sprite"] + "-zh-Hans.png"))
            previews.append((item["sprite"], before, after))
        for target in config["atlases"]:
            name = target["name"]
            check_regions(originals[name], edited[name], regions[name])
            png = name + "-zh-Hans.png"
            edited[name].save(output / png)
            textures.append({"path_id": target["texture_id"],
                             "name": objects[target["texture_id"]].read().m_Name,
                             "png": png, "png_sha256": sha256((output / png).read_bytes()).hexdigest(),
                             "edit_regions": regions[name]})
        manifest = {"schema_version": 1, "assets": [{"entry": entry,
                    "source_sha256": sha256(source).hexdigest(), "textures": textures}],
                    "font_sha256": sha256(font.read_bytes()).hexdigest(),
                    "config_sha256": sha256(config_path.read_bytes()).hexdigest()}
    sheet = Image.new("RGB", (680, len(previews) * 150), "#484858")
    draw = ImageDraw.Draw(sheet)
    for i, (name, before, after) in enumerate(previews):
        draw.text((8, i * 150 + 65), name, fill="white")
        for x, image in ((235, before), (455, after)):
            sheet.paste(image, (x, i * 150 + 10), image)
    sheet.save(output / "comparison.png")
    path = output / "manifest.json"
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--config", type=Path, default=ROOT / "config/button-images.json")
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    print(build(args.base, args.config, args.font, args.output_dir))


if __name__ == "__main__":
    main()
