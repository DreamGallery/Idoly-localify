#!/usr/bin/env python3
"""Export a static Resource Han Rounded SC face from the official 1.910 VF.

The game's Unity Font embedding needs a fixed outline. The upstream VF has a
GPOS lookup that fontTools cannot instantiate (InconsistentGlyphOrder), so
this export removes only GPOS kerning/positioning before fixing the axes.
Chinese glyph shapes and the cmap are unaffected; run font_coverage.py before
building an APK.
"""

import argparse
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="ResourceHanRoundedSC-VF.otf")
    parser.add_argument("output", type=Path, help="static OTF output")
    parser.add_argument("--weight", type=int, default=800, choices=(700, 800, 900))
    args = parser.parse_args()
    font = TTFont(args.source)
    axes = {axis.axisTag for axis in font["fvar"].axes}
    if axes != {"wght", "ROND"}:
        raise ValueError(f"Unexpected variable axes: {axes}")
    if "GPOS" in font:
        del font["GPOS"]
    instantiateVariableFont(
        font, {"wght": args.weight, "ROND": 100}, inplace=True,
        updateFontNames=True, downgradeCFF2=True, static=True,
    )
    if "fvar" in font or "CFF " not in font or font["OS/2"].usWeightClass != args.weight:
        raise ValueError("Export did not produce the requested fixed weight")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    font.save(args.output)
    print(f"{args.weight} weight static OTF: {args.output}")


if __name__ == "__main__":
    main()
