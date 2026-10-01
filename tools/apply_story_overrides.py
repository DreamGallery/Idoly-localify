#!/usr/bin/env python3
"""Apply reviewed story-line corrections with source and prior-translation guards.

The CSV remains editable by translators. This command only changes the exact
lines listed in translations/story-metadata/overrides.json and regenerates their sparse
patches and merged TXT files.
"""

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
from pathlib import Path
import sys

from workspace_paths import story_source_dir, story_csv_dir, story_output_dir, load_module

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
fields = load_module("HoshimiToolkit", "src.adv_csv").fields
merge_file = load_module("HoshimiToolkit", "src.adv_csv").merge_file
save_patch = load_module("HoshimiToolkit", "src.adv_csv").save_patch
validate_translation = load_module("HoshimiToolkit", "src.adv_csv").validate_translation
save_csv = load_module("Hoshimi_Teleprompter", "src.csv_workflow").save_csv


def plan(overrides: list[dict], csv_dir: Path, source_dir: Path):
    """Validate every override before any file is written."""
    grouped = defaultdict(list)
    seen = set()
    for entry in overrides:
        if set(entry) != {"file", "id", "source", "before", "after"}:
            raise ValueError(f"Invalid override keys: {entry}")
        file = entry["file"]
        if not isinstance(file, str) or Path(file).name != file or not file.endswith(".csv"):
            raise ValueError(f"Unsafe CSV filename: {file}")
        key = (file, entry["id"])
        if key in seen:
            raise ValueError(f"Duplicate override: {key}")
        seen.add(key)
        grouped[file].append(entry)

    changes = {}
    for file, entries in grouped.items():
        csv_path = csv_dir / file
        source = source_dir / Path(file).with_suffix(".txt").name
        with csv_path.open(encoding="utf-8", newline="") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != ["id", "name", "text", "trans"]:
                raise ValueError(f"Invalid CSV format: {csv_path}")
            rows = list(reader)
        with source.open(encoding="utf-8", newline="") as stream:
            script = stream.read()
        items = [item for item in fields(script) if item.key != "name"]
        checksum = hashlib.sha256(script.encode("utf-8")).hexdigest()
        if (len(rows) != len(items) + 2 or
                (rows[-2]["id"], rows[-2]["name"], rows[-2]["text"]) !=
                ("info", source.name, checksum) or rows[-1]["id"] != "译者" or
                any(row["id"] != item.identifier or row["text"] != item.source
                    for row, item in zip(rows[:-2], items))):
            raise ValueError(f"CSV source revision differs: {csv_path}")
        row_by_id = {row["id"]: row for row in rows[:-2]}
        item_by_id = {item.identifier: item for item in items}
        for entry in entries:
            item = item_by_id.get(entry["id"])
            row = row_by_id.get(entry["id"])
            if not item or not row or item.source != entry["source"] or row["text"] != item.source:
                raise ValueError(f"Story source changed: {file} {entry['id']}")
            if row["trans"] not in (entry["before"], entry["after"]):
                raise ValueError(f"Translation edited since review: {file} {entry['id']}")
            validate_translation(item, entry["after"])
            if row["trans"] != entry["after"]:
                row["trans"] = entry["after"]
                changes.setdefault(file, []).append(entry["id"])
        changes.setdefault(file, [])
        grouped[file] = rows
    return grouped, changes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    overrides = json.loads((ROOT / "translations/story-metadata/overrides.json").read_text(encoding="utf-8"))
    csv_dir = story_csv_dir(ROOT)
    source_dir = story_source_dir(ROOT)
    patch_dir = ROOT / "translations/story"
    output_dir = story_output_dir(ROOT)
    grouped, changes = plan(overrides, csv_dir, source_dir)
    counts = Counter({"fields": sum(map(len, changes.values())),
                      "files": sum(bool(ids) for ids in changes.values())})
    if args.apply:
        glossary = json.loads((ROOT / "translations/glossary/names.json").read_text(encoding="utf-8"))
        for file, ids in changes.items():
            if not ids:
                continue
            csv_path = csv_dir / file
            source = source_dir / Path(file).with_suffix(".txt").name
            save_csv(csv_path, grouped[file])
            save_patch(source, csv_path, patch_dir / f"{csv_path.stem}.json")
            merge_file(source, csv_path, output_dir / source.name, glossary)
    print(json.dumps(dict(counts), ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
