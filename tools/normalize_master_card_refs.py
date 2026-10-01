#!/usr/bin/env python3
"""Align ShowcaseToy product titles with their referenced MasterDB Card names."""

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re

from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


CARD_ID = re.compile(r"(?:^|_)(card-[a-z0-9-]+)")
QUOTED = re.compile(r"「([^」]+)」")
KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa]")


def plan(orig_dir: Path, zh_dir: Path) -> list[dict[str, str]]:
    card_source = read_table(orig_dir / "Card.json")
    card_target = read_table(zh_dir / "Card.json")
    candidates = defaultdict(set)
    for card_id, fields in card_source.items():
        source_title = fields.get("name")
        translated_title = card_target.get(card_id, {}).get("name")
        if source_title and translated_title and not KANA.search(translated_title):
            candidates[source_title].add(translated_title)
    by_source = {source: next(iter(targets)) for source, targets in candidates.items()
                 if len(targets) == 1}
    toy_source = read_table(orig_dir / "ShowcaseToy.json")
    toy_target = read_table(zh_dir / "ShowcaseToy.json")
    changes = []
    for record_id, fields in toy_source.items():
        found = CARD_ID.search(record_id)
        if not found:
            continue
        card_id = found.group(1)
        original_title = card_source.get(card_id, {}).get("name")
        translated_title = card_target.get(card_id, {}).get("name")
        for field_path, source in fields.items():
            target = toy_target.get(record_id, {}).get(field_path)
            if not target or source.count("「") != 1 or source.count("」") != 1 \
                    or target.count("「") != 1 or target.count("」") != 1:
                continue
            source_quote = QUOTED.search(source)
            target_quote = QUOTED.search(target)
            if not source_quote or not target_quote:
                continue
            quoted_title = source_quote.group(1)
            if original_title and quoted_title != original_title:
                continue
            if not original_title:
                translated_title = by_source.get(quoted_title)
            if not translated_title or KANA.search(translated_title) \
                    or target_quote.group(1) == translated_title:
                continue
            revised = (target[:target_quote.start(1)] + translated_title +
                       target[target_quote.end(1):])
            validate_field(source, revised, f"ShowcaseToy:{record_id}:{field_path}")
            changes.append({"id": record_id, "path": field_path,
                            "card_id": card_id, "source": source,
                            "old": target, "new": revised})
    return changes


def apply(zh_dir: Path, changes: list[dict[str, str]]) -> int:
    path = zh_dir / "ShowcaseToy.json"
    table = read_table(path)
    for item in changes:
        if table.get(item["id"], {}).get(item["path"]) != item["old"]:
            raise ValueError(f"ShowcaseToy:{item['id']}:{item['path']}: translation changed")
        table[item["id"]][item["path"]] = item["new"]
    if changes:
        write_json(path, table)
    return len(changes)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--report", type=Path,
                        default=Path(__file__).resolve().parent.parent /
                        ".analysis/showcase-card-title-review.json")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    changes = plan(args.orig_dir, args.zh_dir)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(changes, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    if args.apply:
        print(f"Aligned {apply(args.zh_dir, changes)} ShowcaseToy titles")
    else:
        print(f"Found {len(changes)} ShowcaseToy titles to align; "
              f"review {args.report} or use --apply")


if __name__ == "__main__":
    main()
