#!/usr/bin/env python3
"""Normalize well identified unit-name variants in translated adventure CSVs.

Only rows containing the corresponding Japanese source term are changed. The
regular CSV -> sparse patch -> TXT path remains the source of truth.
"""

import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import re
import sys

from workspace_paths import story_source_dir, story_csv_dir, story_output_dir, load_module

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
merge_file = load_module("HoshimiToolkit", "src.adv_csv").merge_file
save_patch = load_module("HoshimiToolkit", "src.adv_csv").save_patch
from agency_name import ALIASES as AGENCY_ALIASES, CANONICAL as AGENCY_CANONICAL, SOURCE as AGENCY_SOURCE


RULES = (
    (("月のテンペスト", "月のテンぺスト", "月スト"), "月光风暴", re.compile(
        r"月之暴风雨|月之暴风|月之风暴|月之Tempest|月风暴|月Tempest|"
        r"月光风暴u|月Stou|月Sto|月Sta|月St|月团(?!子)|月暴|月スト|月のテンペスト")),
    (("サニーピース", "サニピ"), "SUNNY PEACE", re.compile(
        r"阳光碎片|阳光和平|太阳和平|Sunny\s*Peace|SunnyPiece|"
        r"SunnyP(?![A-Za-z])|サニーピース|サニピ", re.I)),
    (("TRINITYAiLE", "トリエル"), "TRINITYAiLE", re.compile(
        r"(?<![A-Za-z])(?:TRINEL|TRIEL|TRIELL|TRIELLE|TRINITY)(?![A-Za-z])|トリエル", re.I)),
    (("スリクス", "IIIX"), "IIIX", re.compile(
        r"斯利克斯|斯里克斯|斯里斯|三璃克丝|三日月队|三颗星|ⅢX|スリクス|"
        r"(?<![A-Za-z0-9])(?:Three\s*Cross|Three\s*X|3X|sriXus|srikus|"
        r"Srixon|Srix|Sliks|Surix|SliQx|Slicx|SliX|Sunclu|Sunrise)"
        r"(?![A-Za-z0-9])", re.I)),
    (("リズノワ", "LizNoir"), "LizNoir", re.compile(
        r"莉兹诺瓦|莉兹诺尔|リズノワ|(?<![A-Za-z])(?:LizNoWa|LizNoW|"
        r"LisNoWa|LisNoW|LisNoir|LizNoah|LizNoa|LizNoir)(?![A-Za-z])", re.I)),
    (("ぱじゃパ！",), "睡衣派对", re.compile(r"ぱじゃパ！")),
    (("萌え萌えキュン",), "萌萌啾", re.compile(r"萌え萌えキュン")),
    ((AGENCY_SOURCE,), AGENCY_CANONICAL, AGENCY_ALIASES),
)


def normalize(source: str, translation: str) -> tuple[str, list[str]]:
    changed = []
    for source_terms, canonical, pattern in RULES:
        if any(term in source for term in source_terms):
            translation, count = pattern.subn(canonical, translation)
            if count:
                changed.append(canonical)
    return translation, changed


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
            result, terms = normalize(row["text"], row["trans"])
            if result != row["trans"]:
                for term in terms:
                    totals[term] += 1
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
