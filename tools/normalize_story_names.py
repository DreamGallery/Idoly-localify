#!/usr/bin/env python3
"""Apply reviewed character-name corrections to ADV CSV and rebuilt TXT."""

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import sys

from workspace_paths import story_source_dir, story_csv_dir, story_output_dir, load_module

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
merge_file = load_module("HoshimiToolkit", "src.adv_csv").merge_file
save_patch = load_module("HoshimiToolkit", "src.adv_csv").save_patch
from normalize_master_names import normalize


def process(csv_dir: Path, source_dir: Path, patch_dir: Path, output_dir: Path,
            name_glossary: dict[str, str], apply: bool) -> Counter:
    totals = Counter()
    for csv_path in sorted(csv_dir.glob("adv_*.csv")):
        with csv_path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["id", "name", "text", "trans"]:
                raise ValueError(f"Invalid CSV format: {csv_path}")
            rows = list(reader)
        changed = False
        for row in rows[:-2]:
            if not row["trans"]:
                continue
            result = normalize(row["text"], row["trans"], "ADV",
                               csv_path.stem, row["id"])
            if result != row["trans"]:
                totals["fields"] += 1
                row["trans"] = result
                changed = True
        if not changed:
            continue
        totals["files"] += 1
        if not apply:
            continue
        save_csv = load_module("Hoshimi_Teleprompter", "src.csv_workflow").save_csv
        save_csv(csv_path, rows)
        name = csv_path.with_suffix(".txt").name
        source = source_dir / name
        save_patch(source, csv_path, patch_dir / f"{csv_path.stem}.json")
        merge_file(source, csv_path, output_dir / name, name_glossary)
    return totals


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    glossary = json.loads((ROOT / "translations/glossary/names.json").read_text(
        encoding="utf-8"))
    totals = process(story_csv_dir(ROOT),
                     story_source_dir(ROOT),
                     ROOT / "translations/story",
                     story_output_dir(ROOT),
                     glossary, args.apply)
    print(json.dumps(dict(totals), ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
