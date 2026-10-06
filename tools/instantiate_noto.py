#!/usr/bin/env python3
"""Build the pinned static Noto CJK face from the official variable font."""
from __future__ import annotations

import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path


def instantiate_font(data, spec):
    from fontTools.ttLib import TTFont
    from fontTools.varLib.instancer import instantiateVariableFont

    if hashlib.sha256(data).hexdigest() != spec['source']['sha256']:
        raise ValueError('Variable font source SHA-256 mismatch')
    instance = spec['instance']
    with TTFont(BytesIO(data), recalcTimestamp=False) as font:
        if 'fvar' not in font or 'CFF2' not in font:
            raise ValueError('Expected the pinned Noto CJK CFF2 variable font')
        axes = {axis.axisTag: axis for axis in font['fvar'].axes}
        limits = instance['axes']
        if (set(axes) != set(limits) or limits.get('wght') != spec['weight'] or
                any(not axes[tag].minValue <= value <= axes[tag].maxValue
                    for tag, value in limits.items())):
            raise ValueError('Invalid static font instance axes')
        cmap = {cp: font.getGlyphID(name) for cp, name in font.getBestCmap().items()}
        instantiateVariableFont(font, limits, inplace=True, updateFontNames=False,
                                downgradeCFF2=True, static=True)
        family, style = instance['family'], instance['style']
        full_name, postscript = family + ' ' + style, instance['postscript_name']
        names = {1: spec['family'], 2: spec['style'],
                 3: spec['version'] + ';Localify;' + postscript,
                 4: full_name, 6: postscript, 16: family, 17: style}
        for name_id, value in names.items():
            font['name'].removeNames(nameID=name_id)
            font['name'].setName(value, name_id, 3, 1, 0x409)
        font['name'].removeNames(nameID=25)
        font['OS/2'].usWeightClass = spec['weight']
        font['OS/2'].fsSelection &= ~((1 << 0) | (1 << 5))
        font['OS/2'].fsSelection |= 1 << 6
        font['head'].macStyle &= ~3
        cff = font['CFF '].cff
        cff.fontNames = [postscript]
        top = cff.topDictIndex[0]
        top.FamilyName, top.FullName, top.Weight = family, full_name, style.capitalize()
        if ('fvar' in font or 'CFF2' in font or
                {cp: font.getGlyphID(name) for cp, name in font.getBestCmap().items()} != cmap):
            raise ValueError('Static font conversion changed character coverage')
        stream = BytesIO()
        font.save(stream)
    result = stream.getvalue()
    if hashlib.sha256(result).hexdigest() != spec['sha256']:
        raise ValueError('Static font output SHA-256 mismatch; use pinned fonttools version')
    return result


def main():
    from prepare_module_font import atomic_write, verify_font
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--config', type=Path,
                        default=Path(__file__).resolve().parents[1] / 'config/font-source.json')
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Output must not overwrite the variable source font')
    spec = json.loads(args.config.read_text())
    result = instantiate_font(args.source.read_bytes(), spec)
    verify_font(result, spec)
    atomic_write(args.output, result)
    print(f"Prepared {spec['weight']} weight static font: {args.output}")


if __name__ == '__main__':
    main()
