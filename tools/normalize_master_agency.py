#!/usr/bin/env python3
"""Normalize the IIIX agency proper name in translated MasterDB fields."""

import argparse
from collections import Counter
import json
from pathlib import Path

from agency_name import normalize
from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


def run(orig_dir: Path, zh_dir: Path, apply: bool) -> Counter:
    totals = Counter()
    for source_file in sorted(orig_dir.glob("*.json")):
        source = read_table(source_file)
        translated = read_table(zh_dir / source_file.name)
        changed = False
        for record_id, paths in source.items():
            for field_path, original in paths.items():
                target = translated[record_id][field_path]
                revised = normalize(original, target)
                if revised == target:
                    continue
                validate_field(original, revised,
                               f"{source_file.stem}:{record_id}:{field_path}")
                translated[record_id][field_path] = revised
                totals["fields"] += 1
                changed = True
        if changed:
            totals["tables"] += 1
            if apply:
                write_json(zh_dir / source_file.name, translated)
    return totals


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    print(json.dumps(dict(run(args.orig_dir, args.zh_dir, args.apply)),
                     ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
