#!/usr/bin/env python3
"""Fill MasterDB singer fields from the shared name and group glossaries.

Only new or empty singer translations are filled. Existing reviewed translations
remain authoritative, and unresolved Japanese names are reported for review.
"""

import argparse
import json
from pathlib import Path
import re

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


ROOT = Path(__file__).resolve().parent.parent
KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa]")


def compose(source: str, names: dict[str, str], terms: dict[str, str]) -> str:
    replacements = {**names, **terms}
    result = source
    for original in sorted(replacements, key=len, reverse=True):
        translated = replacements[original]
        if original != translated:
            result = result.replace(original, translated)
    return result


def fill(orig_dir: Path, zh_dir: Path, names: dict[str, str],
         terms: dict[str, str]) -> tuple[int, int, dict[str, str]]:
    filled = kept = 0
    unresolved = {}
    for source_file in sorted(orig_dir.glob("*.json")):
        source_table = read_table(source_file)
        translated_table = read_table(zh_dir / source_file.name)
        changed = False
        for record_id, fields in source_table.items():
            original = fields.get("singer")
            if original is None:
                continue
            previous = translated_table.get(record_id, {}).get("singer", "")
            if previous:
                kept += 1
                continue
            candidate = compose(original, names, terms)
            if KANA.search(candidate):
                unresolved[original] = candidate
                continue
            validate_field(original, candidate, f"{source_file.stem}:{record_id}:singer")
            translated_table.setdefault(record_id, {})["singer"] = candidate
            filled += 1
            changed = True
        if changed:
            write_json(zh_dir / source_file.name, translated_table)
    return filled, kept, unresolved


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--name-glossary", type=Path,
                        default=ROOT / "translations/glossary/names.json")
    parser.add_argument("--term-glossary", type=Path,
                        default=ROOT / "translations/glossary/terms.json")
    args = parser.parse_args()
    names = json.loads(args.name_glossary.read_text(encoding="utf-8"))
    terms = json.loads(args.term_glossary.read_text(encoding="utf-8"))
    if not all(isinstance(item, dict) and all(
            isinstance(key, str) and isinstance(value, str) and value
            for key, value in item.items()) for item in (names, terms)):
        parser.error("glossaries must map source strings to nonempty translations")
    filled, kept, unresolved = fill(args.orig_dir, args.zh_dir, names, terms)
    print(f"Filled {filled} singer fields; kept {kept} reviewed fields; "
          f"{len(unresolved)} unresolved source names")
    for source, candidate in sorted(unresolved.items()):
        print(f"  {source!r} -> {candidate!r}")


if __name__ == "__main__":
    main()
