#!/usr/bin/env python3
"""Align MasterDB Card, Gacha, EventStory and Music title references."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


ROOT = Path(__file__).resolve().parent.parent
KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa]")
QUOTES = (("「", "」"), ("『", "』"), ("《", "》"), ("“", "”"))


def title_map(orig_dir: Path, zh_dir: Path) -> dict[str, str]:
    candidates = defaultdict(set)
    for table_name, title_path in (("Card", "name"), ("Gacha", "name"),
                                   ("EventStory", "description"),
                                   ("Music", "name")):
        source_table = read_table(orig_dir / f"{table_name}.json")
        target_table = read_table(zh_dir / f"{table_name}.json")
        for record_id, fields in source_table.items():
            source = fields.get(title_path)
            target = target_table.get(record_id, {}).get(title_path)
            if source and target and not KANA.search(target):
                candidates[source].add(target)
    return {source: next(iter(targets)) for source, targets in candidates.items()
            if len(targets) == 1}


def quoted_pair(source: str, target: str):
    for source_open, source_close in QUOTES:
        if source.count(source_open) != 1 or source.count(source_close) != 1:
            continue
        source_start = source.index(source_open) + 1
        source_end = source.index(source_close, source_start)
        for target_open, target_close in QUOTES:
            if target.count(target_open) != 1 or target.count(target_close) != 1:
                continue
            target_start = target.index(target_open) + 1
            target_end = target.index(target_close, target_start)
            return (source[source_start:source_end],
                    target[target_start:target_end], target_start, target_end)
    return None


def plan(orig_dir: Path, zh_dir: Path, only_kana: bool = False,
         tables: set[str] | None = None) -> list[dict[str, str]]:
    titles = title_map(orig_dir, zh_dir)
    changes = []
    for source_path in sorted(orig_dir.glob("*.json")):
        table_name = source_path.stem
        if table_name == "ShowcaseToy":
            continue  # Card-ID matching in normalize_master_card_refs.py is stronger.
        if tables is not None and table_name not in tables:
            continue
        source_table = read_table(source_path)
        target_table = read_table(zh_dir / source_path.name)
        for record_id, fields in source_table.items():
            for field_path, source in fields.items():
                target = target_table.get(record_id, {}).get(field_path)
                if not target:
                    continue
                if table_name == "HomeTalk" and field_path == "displayConditionDescription" \
                        and " fran\n" in source and " fran\n" in target:
                    original_title, _ = source.split(" fran\n", 1)
                    current_title, target_tail = target.split(" fran\n", 1)
                    canonical = titles.get(original_title)
                    if canonical and canonical != current_title and (
                            not only_kana or KANA.search(current_title)):
                        revised = canonical + " fran\n" + target_tail
                        validate_field(source, revised,
                                       f"{table_name}:{record_id}:{field_path}")
                        changes.append({"table": table_name, "id": record_id,
                                        "path": field_path, "title": original_title,
                                        "old": target, "new": revised})
                    continue
                pair = quoted_pair(source, target)
                if not pair:
                    continue
                original_title, current_title, start, end = pair
                if only_kana and not KANA.search(current_title):
                    continue
                canonical = titles.get(original_title)
                if not canonical or canonical == current_title:
                    continue
                revised = target[:start] + canonical + target[end:]
                validate_field(source, revised, f"{table_name}:{record_id}:{field_path}")
                changes.append({"table": table_name, "id": record_id,
                                "path": field_path, "title": original_title,
                                "old": target, "new": revised})
    return changes


def apply(zh_dir: Path, changes: list[dict[str, str]]) -> int:
    by_table = defaultdict(list)
    for item in changes:
        by_table[item["table"]].append(item)
    for table_name, items in by_table.items():
        path = zh_dir / f"{table_name}.json"
        table = read_table(path)
        for item in items:
            if table[item["id"]][item["path"]] != item["old"]:
                raise ValueError(f"{table_name}:{item['id']}:{item['path']}: changed")
            table[item["id"]][item["path"]] = item["new"]
        write_json(path, table)
    return len(changes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--report", type=Path,
                        default=ROOT / ".analysis/master-title-ref-review.json")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--kana-only", action="store_true",
                        help="Only align quoted translations that still contain kana")
    parser.add_argument("--table", action="append", default=[],
                        help="Limit reference updates to this MasterDB table; repeatable")
    args = parser.parse_args()
    changes = plan(args.orig_dir, args.zh_dir, args.kana_only,
                   set(args.table) if args.table else None)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(changes, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    if args.apply:
        print(f"Aligned {apply(args.zh_dir, changes)} title references")
    else:
        print(f"Found {len(changes)} title references to align: {args.report}")


if __name__ == "__main__":
    main()
