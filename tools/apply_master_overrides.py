#!/usr/bin/env python3
"""Apply reviewed source-to-translation corrections to MasterDB JSON dictionaries."""

import argparse
import json
from pathlib import Path

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


ROOT = Path(__file__).resolve().parent.parent


def apply(orig_dir: Path, zh_dir: Path, overrides: dict[str, str]):
    tables = fields = 0
    used = set()
    for source_file in sorted(orig_dir.glob("*.json")):
        source = read_table(source_file)
        translated = read_table(zh_dir / source_file.name)
        changed = False
        for record_id, paths in source.items():
            for field_path, original in paths.items():
                revised = overrides.get(original)
                if revised is None:
                    continue
                used.add(original)
                validate_field(original, revised,
                               f"{source_file.stem}:{record_id}:{field_path}")
                if translated.get(record_id, {}).get(field_path) != revised:
                    translated.setdefault(record_id, {})[field_path] = revised
                    fields += 1
                    changed = True
        if changed:
            write_json(zh_dir / source_file.name, translated)
            tables += 1
    missing = set(overrides) - used
    if missing:
        raise ValueError(f"{len(missing)} override sources no longer exist in MasterDB")
    return tables, fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--overrides", type=Path,
                        default=ROOT / "translations/master/overrides.json")
    args = parser.parse_args()
    overrides = json.loads(args.overrides.read_text(encoding="utf-8"))
    if not isinstance(overrides, dict) or any(
            not isinstance(source, str) or not isinstance(value, str) or not value
            for source, value in overrides.items()):
        parser.error("overrides must map source strings to nonempty translated strings")
    tables, fields = apply(args.orig_dir, args.zh_dir, overrides)
    print(f"Applied {fields} reviewed corrections across {tables} MasterDB tables")


if __name__ == "__main__":
    main()
