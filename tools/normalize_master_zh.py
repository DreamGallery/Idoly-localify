#!/usr/bin/env python3
"""Convert MasterDB translated JSON values to Simplified Chinese in place."""

import argparse
from pathlib import Path

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


def normalize(orig_dir: Path, zh_dir: Path, convert) -> tuple[int, int]:
    tables = fields = 0
    for source_file in sorted(orig_dir.glob("*.json")):
        source = read_table(source_file)
        translated = read_table(zh_dir / source_file.name)
        changed = False
        for record_id, paths in translated.items():
            for field_path, value in paths.items():
                revised = convert(value)
                if revised == value:
                    continue
                original = source.get(record_id, {}).get(field_path)
                if original is None:
                    raise ValueError(f"{source_file.stem}:{record_id}:{field_path}: no source")
                validate_field(original, revised,
                               f"{source_file.stem}:{record_id}:{field_path}")
                paths[field_path] = revised
                fields += 1
                changed = True
        if changed:
            write_json(zh_dir / source_file.name, translated)
            tables += 1
    return tables, fields


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    args = parser.parse_args()
    try:
        from opencc import OpenCC
    except ImportError as error:
        raise SystemExit("Install opencc-python-reimplemented first") from error
    tables, fields = normalize(args.orig_dir, args.zh_dir, OpenCC("t2s").convert)
    print(f"Converted {fields} translated fields to Simplified Chinese in {tables} tables")


if __name__ == "__main__":
    main()
