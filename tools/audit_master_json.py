#!/usr/bin/env python3
"""Report untranslated or inconsistent MasterDB JSON entries; normalize exact terms."""

import argparse
from collections import Counter
import json
from pathlib import Path
import re

from localize import write_json
from master_json import ORIG, ZH, read_table


ROOT = Path(__file__).resolve().parent.parent
# U+30FB is the middle dot used between Chinese title parts; U+30FC is a
# prolonged-sound mark. Neither alone means Japanese text was left untranslated.
KANA = re.compile(r"[\u3041-\u3096\u30a1-\u30fa]")


def review_item(table, record_id, field_path, source, target, **extra):
    return {"table": table, "id": record_id, "path": field_path,
            "source_excerpt": source[:160], "translation_excerpt": target[:160],
            **extra}


def audit(orig_dir: Path, zh_dir: Path, terms: dict, apply: bool = False,
          aliases: dict | None = None, retained_titles: dict | None = None):
    aliases = aliases or {}
    retained_titles = retained_titles or {}
    counts = Counter()
    issues = {"term": [], "kana": [], "retained": [], "unchanged": [],
              "prolonged_mark": []}
    for path in sorted(orig_dir.glob("*.json")):
        source_table = read_table(path)
        translation_table = read_table(zh_dir / path.name)
        changed = False
        for record_id, fields in source_table.items():
            for field_path, source in fields.items():
                target = translation_table.get(record_id, {}).get(field_path, "")
                if not target:
                    counts["untranslated"] += 1
                    continue
                if (field_path in {"lyricist", "composer", "arranger"} and
                        target == source):
                    counts["retained_credit"] += 1
                    continue
                if target == source:
                    counts["unchanged"] += 1
                    issues["unchanged"].append(review_item(
                        path.stem, record_id, field_path, source, target))
                if KANA.search(target):
                    remaining = target
                    matched_titles = []
                    for title in retained_titles:
                        if title in source and title in remaining:
                            remaining = remaining.replace(title, "")
                            matched_titles.append(title)
                    if KANA.search(remaining):
                        counts["kana_remaining"] += 1
                        issues["kana"].append(review_item(
                            path.stem, record_id, field_path, source, target))
                    else:
                        counts["retained_kana"] += 1
                        issues["retained"].append(review_item(
                            path.stem, record_id, field_path, source, target,
                            titles=matched_titles))
                elif "ー" in target:
                    # A lone Japanese long-vowel mark in an otherwise Chinese
                    # line is commonly left by machine translation. Katakana
                    # titles are handled by the kana review above.
                    counts["prolonged_mark"] += 1
                    issues["prolonged_mark"].append(review_item(
                        path.stem, record_id, field_path, source, target))
                for name, canonical in terms.items():
                    if name not in source or canonical in target:
                        continue
                    alternatives = sorted({name, *aliases.get(name, [])},
                                          key=len, reverse=True)
                    matches = [alias for alias in alternatives if alias in target]
                    if matches and apply:
                        for alias in matches:
                            target = target.replace(alias, canonical)
                        translation_table[record_id][field_path] = target
                        changed = True
                        counts["terms_normalized"] += 1
                    else:
                        counts["term_review"] += 1
                        issues["term"].append(review_item(
                            path.stem, record_id, field_path, source, target,
                            term=name))
        if changed:
            write_json(zh_dir / path.name, translation_table)
    return dict(counts), issues


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--terms", type=Path, default=ROOT / "translations/glossary/terms.json")
    parser.add_argument("--aliases", type=Path,
                        default=ROOT / "translations/glossary/term-aliases.json")
    parser.add_argument("--retained-titles", type=Path,
                        default=ROOT / "translations/master/retained-titles.json")
    parser.add_argument("--report", type=Path,
                        default=ROOT / ".analysis/reports/master-review.json")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    terms = json.loads(args.terms.read_text(encoding="utf-8"))
    aliases = json.loads(args.aliases.read_text(encoding="utf-8"))
    retained_titles = json.loads(args.retained_titles.read_text(encoding="utf-8"))
    if not isinstance(aliases, dict) or any(
            key not in terms or not isinstance(values, list) or any(
                not isinstance(value, str) or not value or value == terms[key]
                for value in values) for key, values in aliases.items()):
        parser.error("aliases must map known terms to lists of distinct nonempty aliases")
    if not isinstance(retained_titles, dict) or any(
            not isinstance(title, str) or not title or not isinstance(url, str)
            or not url.startswith("https://") for title, url in retained_titles.items()):
        parser.error("retained titles must map exact source strings to HTTPS sources")
    counts, issues = audit(args.orig_dir, args.zh_dir, terms, args.apply,
                           aliases, retained_titles)
    write_json(args.report, {"counts": counts, "term_issues": issues["term"],
                             "kana_issues": issues["kana"],
                             "prolonged_mark_issues": issues["prolonged_mark"],
                             "retained_kana": issues["retained"],
                             "unchanged_issues": issues["unchanged"]})
    print(json.dumps(counts, ensure_ascii=False))


if __name__ == "__main__":
    main()
