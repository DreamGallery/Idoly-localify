#!/usr/bin/env python3
"""Extract conflict-free UI episode titles from translated adventure CSV files."""

import argparse
from collections import defaultdict
import csv
from pathlib import Path
import re

from workspace_paths import story_csv_dir

from localize import ROOT, validate, write_json

SOURCE_SUFFIX = re.compile(r"\d+話\s+(.+)$")
TARGET_SUFFIX = re.compile(r"\d+话\s+(.+)$")


def collect_titles(csv_dir: Path) -> tuple[dict[str, str], dict[str, list[str]]]:
    candidates: dict[str, set[str]] = defaultdict(set)
    for path in sorted(csv_dir.glob("*.csv")):
        with path.open(encoding="utf-8-sig", newline="") as stream:
            for row in csv.DictReader(stream):
                if ":title:" not in row["id"]:
                    continue
                original, translated = row["text"], row["trans"]
                if not original or not translated or original == translated:
                    continue
                validate(original, translated, f"{path.name}:{row['id']}")
                candidates[original].add(translated)
                source_suffix = SOURCE_SUFFIX.search(original)
                target_suffix = TARGET_SUFFIX.search(translated)
                if source_suffix and target_suffix:
                    source_title, target_title = source_suffix.group(1), target_suffix.group(1)
                    if source_title != target_title:
                        validate(source_title, target_title, f"{path.name}:{row['id']}:suffix")
                        candidates[source_title].add(target_title)
    safe = {original: next(iter(values)) for original, values in candidates.items()
            if len(values) == 1}
    conflicts = {original: sorted(values) for original, values in candidates.items()
                 if len(values) > 1}
    return safe, conflicts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--csv-dir", type=Path,
                        default=story_csv_dir(ROOT))
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/compiled/story-title-map.json")
    parser.add_argument("--conflicts", type=Path,
                        default=ROOT / ".analysis/story-title-conflicts.json")
    args = parser.parse_args()
    safe, conflicts = collect_titles(args.csv_dir)
    write_json(args.output, safe)
    write_json(args.conflicts, conflicts)
    print(f"Wrote {len(safe)} conflict-free title mappings; "
          f"{len(conflicts)} conflicting sources need review")


if __name__ == "__main__":
    main()
