#!/usr/bin/env python3
"""Replace leftover Japanese long-vowel marks in Chinese MasterDB JSON lines."""

import argparse
from pathlib import Path
import re

from audit_master_json import KANA
from localize import write_json
from master_json import ZH, read_table


MARKS = re.compile(r"ー+")
SPECIAL = {
    ("Story", "st-card-cca-05-goch-00-03", "name"):
        "那只兔子\n先生——\n——！！！",
    ("Story", "st-card-mna-05-birt-01-01", "name"):
        "和平时\n不一样！",
}


def normalize(table: str, record_id: str, field_path: str, target: str) -> str:
    if "ー" not in target or KANA.search(target):
        return target
    special = SPECIAL.get((table, record_id, field_path))
    if special is not None:
        return special
    return MARKS.sub(lambda match: "～" if len(match.group()) == 1 else "——",
                     target)


def run(zh_dir: Path, apply: bool) -> list[tuple[str, str, str, str, str]]:
    changes = []
    for path in sorted(zh_dir.glob("*.json")):
        data = read_table(path)
        changed = False
        for record_id, fields in data.items():
            for field_path, target in fields.items():
                translated = normalize(path.stem, record_id, field_path, target)
                if translated == target:
                    continue
                changes.append((path.stem, record_id, field_path, target, translated))
                fields[field_path] = translated
                changed = True
        if changed and apply:
            write_json(path, data)
    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    changes = run(args.zh_dir, args.apply)
    for table, record_id, field_path, before, after in changes:
        print(f"{table}/{record_id}/{field_path}: {before!r} -> {after!r}")
    print(f"{len(changes)} fields {'updated' if args.apply else 'would change'}")


if __name__ == "__main__":
    main()
