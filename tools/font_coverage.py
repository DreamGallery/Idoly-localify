#!/usr/bin/env python3
"""Check that a replacement font covers text it may render."""

from __future__ import annotations

import argparse
from io import BytesIO
import json
import os
from pathlib import Path
import sys
import unicodedata

from localize import write_json

from workspace_paths import story_source_dir

ROOT = Path(__file__).resolve().parents[1]
VARIATION_SELECTORS = {"\ufe0e", "\ufe0f"}


def translated_fields(root: Path):
    story_dir = root / "translations/story"
    master_dir = root / "translations/master/zh-Hans"
    required = [root / "translations/ui/zh-Hans.json",
                root / "translations/glossary/names.json",
                root / "translations/glossary/terms.json"]
    if not any(story_dir.glob("adv_*.json")) or not any(master_dir.glob("*.json")):
        raise ValueError("Story or MasterDB translations are missing")
    for path in required:
        if not path.is_file():
            raise ValueError(f"Translation input is missing: {path}")

    def strings(value, label):
        if isinstance(value, str):
            if value:
                yield label, value
        elif isinstance(value, dict):
            for key, child in value.items():
                yield from strings(child, f"{label}/{key}")
        else:
            raise ValueError(f"{label}: translation must be a string or dictionary")

    for path in sorted(story_dir.glob("adv_*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or not isinstance(data.get("translations"), dict):
            raise ValueError(f"{path}: invalid story patch")
        yield from strings(data["translations"], str(path.relative_to(root)))
    for path in sorted(master_dir.glob("*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError(f"{path}: invalid MasterDB translation table")
        yield from strings(data, str(path.relative_to(root)))
    for path in required:
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            raise ValueError(f"{path}: invalid translation dictionary")
        yield from strings(data, str(path.relative_to(root)))


def source_fields(root: Path):
    """Full font replacement also renders original story, MasterDB and UI text."""
    groups = ((story_source_dir(root), "adv_*.txt"),
              (root / "translations/master/orig", "*.json"),
              (root / "translations/ui", "source.json"),
              (root / "translations/ui", "variants-source.json"),
              (root / "app/src/main/assets/idoly-localify", "dict.json"))
    for directory, pattern in groups:
        files = sorted(directory.glob(pattern))
        if not files:
            raise ValueError(f"Source text for whole-font replacement is missing: {directory}/{pattern}")
        for path in files:
            yield os.path.relpath(path, root), path.read_text(encoding="utf-8")


def replaces_all_text(root: Path) -> bool:
    dictionary = root / "app/src/main/assets/idoly-localify/dict.json"
    if not dictionary.is_file():
        raise ValueError(f"Font mode is unavailable: {dictionary}")
    data = json.loads(dictionary.read_text(encoding="utf-8"))
    return data.get("font_mode") in ("replace-all", "preserve-non-cjk", "replace-ui")


def visible(character: str) -> bool:
    return (character not in VARIATION_SELECTORS and
            unicodedata.category(character) not in {"Cc", "Cf", "Cs", "Cn"})


def audit_codepoints(codepoints: set[int], fields) -> dict:
    missing = set()
    invalid = []
    affected = 0
    examples = []
    total = 0
    for label, translated in fields:
        total += 1
        if "\ufffd" in translated:
            invalid.append(label)
        absent = {character for character in translated
                  if visible(character) and ord(character) not in codepoints}
        if absent:
            affected += 1
            missing.update(absent)
            if len(examples) < 20:
                examples.append({"field": label, "missing": "".join(sorted(absent))})
    return {"fields": total, "affected_fields": affected,
            "missing_characters": "".join(sorted(missing)),
            "replacement_character_fields": invalid[:20],
            "replacement_character_count": len(invalid),
            "examples": examples}


def audit_font(font: Path, root: Path = ROOT, face: int | None = None) -> dict:
    from fontTools.ttLib import TTFont
    from patch_font import font_bytes

    data = font_bytes(font, face)
    cmap = TTFont(BytesIO(data)).getBestCmap()
    all_text = replaces_all_text(root)
    def fields():
        yield from translated_fields(root)
        if all_text:
            yield from source_fields(root)
    report = audit_codepoints(set(cmap), fields())
    report["source_fields_included"] = all_text
    return report


def require_coverage(report: dict) -> None:
    if report["replacement_character_count"]:
        raise ValueError("Translations contain U+FFFD replacement characters: " +
                         ", ".join(report["replacement_character_fields"][:3]))
    if report["missing_characters"]:
        missing = report["missing_characters"]
        raise ValueError(f"Replacement font misses {len(missing)} visible "
                         f"characters across {report['affected_fields']} fields: "
                         f"{missing[:30]!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--face", type=int)
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/font-coverage.json")
    args = parser.parse_args()
    try:
        report = audit_font(args.font, args.root, args.face)
        write_json(args.output, report)
        require_coverage(report)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"Font covers all visible characters in {report['fields']} text inputs; "
          f"report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
