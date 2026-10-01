#!/usr/bin/env python3
"""Normalize source-verified character name mistranslations in MasterDB JSON."""

import argparse
from pathlib import Path

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


SATOMI_SPELLINGS = ("さとみ", "さとむ")  # The latter is a typo in a chat scene.


def normalize(source: str, target: str, table: str, record_id: str,
              field_path: str) -> str:
    if not any(name in source for name in SATOMI_SPELLINGS) or "里见" not in target:
        return target
    result = (target.replace("里见先生", "里美小姐")
                    .replace("里见女士", "里美小姐")
                    .replace("里见", "里美"))
    if (table, record_id, field_path) == (
            "Message", "message-event-ladder-24-1208-01", "details[38].text"):
        result = result.replace("让他给我", "让她给我")
    return result


def run(orig_dir: Path, zh_dir: Path, apply: bool):
    changes = []
    for path in sorted(orig_dir.glob("*.json")):
        original = read_table(path)
        translated = read_table(zh_dir / path.name)
        changed = False
        for record_id, fields in original.items():
            for field_path, source in fields.items():
                target = translated[record_id][field_path]
                revised = normalize(source, target, path.stem, record_id, field_path)
                if revised == target:
                    continue
                validate_field(source, revised, f"{path.stem}:{record_id}:{field_path}")
                translated[record_id][field_path] = revised
                changes.append((path.stem, record_id, field_path, target, revised))
                changed = True
        if changed and apply:
            write_json(zh_dir / path.name, translated)
    return changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    changes = run(args.orig_dir, args.zh_dir, args.apply)
    for table, record_id, field_path, before, after in changes:
        print(f"{table}/{record_id}/{field_path}: {before!r} -> {after!r}")
    print(f"{len(changes)} fields {'updated' if args.apply else 'would change'}")


if __name__ == "__main__":
    main()
