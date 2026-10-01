#!/usr/bin/env python3
"""Align ConditionDescription card, costume and song references by MasterDB ID."""

import argparse
from collections import Counter
import json
from pathlib import Path

from audit_master_json import KANA
from localize import write_json
from master_json import ORIG, ZH, read_table, validate_field


ROOT = Path(__file__).resolve().parent.parent
POSSESSION_SUFFIXES = {
    "を持っている": "持有",
    "を持っていない": "未持有",
    "を持っていてレアリティ9以下": "持有且稀有度不高于9",
}


def referenced_record(condition_id: str, table: str, ids: list[str]) -> str | None:
    for record_id in ids:
        possession = "cd-possession-" + record_id
        if table in {"Card", "Costume"} and (
                condition_id == possession or condition_id.startswith(possession + "-")):
            return record_id
        dream_card = "obtain-" + record_id
        if table == "Card" and condition_id.startswith("cd-dream-ticket-") and (
                condition_id.endswith(dream_card) or dream_card + "-" in condition_id):
            return record_id
        if table == "Music" and condition_id.startswith("cd-time-" + record_id + "-"):
            return record_id
    return None


def translate_character(name: str, names: dict[str, str]) -> str:
    if not name:
        return ""
    if name not in names:
        raise ValueError(f"Unknown character reference: {name!r}")
    return names[name]


def normalize(condition_id: str, source: str, target: str, table: str,
              record_id: str, original: dict, translated: dict,
              names: dict[str, str]) -> str:
    if not KANA.search(target):
        return target
    source_name = original[record_id].get("name")
    chinese_name = translated[record_id].get("name")
    if not source_name or not chinese_name or source_name not in source:
        return target
    if table == "Costume":
        return target.replace(source_name, chinese_name) if source_name in target else target
    if table == "Music":
        for japanese_prefix, chinese_prefix in (("開始：", "开始："), ("期間：", "期间：")):
            if source == japanese_prefix + source_name + "楽曲":
                return chinese_prefix + chinese_name + "乐曲"
        return target
    if table == "Card" and condition_id.startswith("cd-dream-ticket-"):
        remainder = source[len(source_name):]
        if not remainder.endswith("_所持"):
            return target
        character = translate_character(remainder[:-len("_所持")].strip(), names)
        return "持有「" + chinese_name + "」" + character
    if table == "Card" and condition_id.startswith("cd-possession-"):
        remainder = source[len(source_name):].strip()
        for suffix, status in POSSESSION_SUFFIXES.items():
            if remainder.endswith(suffix):
                character = translate_character(remainder[:-len(suffix)].strip(), names)
                return status + "「" + chinese_name + "」" + character
    return target


def normalize_internal(condition_id: str, source: str, target: str) -> str:
    if condition_id.startswith("cd-ms-daily-") and source.endswith("_閉じ"):
        for ending in ("_閉じ", "_闭じ"):
            if target.endswith(ending):
                return target[:-len(ending)] + "_关闭"
    if (condition_id.startswith("cd-time-medal-buttonbadge-birthday-") and
            source.endswith("缶バッジ交換") and target.endswith("缶バッジ交換")):
        return target[:-len("缶バッジ交換")] + "徽章兑换"
    if (condition_id == "cd-possession-card-kor-05-prem-00-not-rarity-max" and
            source == "dress up venus franを持っていてレアリティ9以下"):
        return "持有「dress up venus」fran，稀有度不高于9"
    return target


def run(orig_dir: Path, zh_dir: Path, names: dict[str, str], apply: bool):
    condition_orig = read_table(orig_dir / "ConditionDescription.json")
    condition_zh = read_table(zh_dir / "ConditionDescription.json")
    refs = {}
    for table in ("Card", "Costume", "Music"):
        original = read_table(orig_dir / f"{table}.json")
        translated = read_table(zh_dir / f"{table}.json")
        refs[table] = (sorted(original, key=len, reverse=True), original, translated)
    totals = Counter()
    changes = []
    for condition_id, fields in condition_orig.items():
        source = fields.get("description")
        target = condition_zh[condition_id].get("description")
        if not source or not target or not KANA.search(target):
            continue
        revised = normalize_internal(condition_id, source, target)
        if revised != target:
            validate_field(source, revised,
                           f"ConditionDescription:{condition_id}:description")
            changes.append((condition_id, "Internal", "", target, revised))
            condition_zh[condition_id]["description"] = revised
            totals["Internal"] += 1
            continue
        for table, (ids, original, translated) in refs.items():
            record_id = referenced_record(condition_id, table, ids)
            if not record_id:
                continue
            revised = normalize(condition_id, source, target, table, record_id,
                                original, translated, names)
            if revised != target:
                validate_field(source, revised,
                               f"ConditionDescription:{condition_id}:description")
                changes.append((condition_id, table, record_id, target, revised))
                condition_zh[condition_id]["description"] = revised
                totals[table] += 1
            break
    if changes and apply:
        write_json(zh_dir / "ConditionDescription.json", condition_zh)
    return changes, totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--names", type=Path,
                        default=ROOT / "translations/glossary/names.json")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    names = json.loads(args.names.read_text(encoding="utf-8"))
    changes, totals = run(args.orig_dir, args.zh_dir, names, args.apply)
    for condition_id, table, record_id, before, after in changes:
        print(f"{condition_id} [{table}:{record_id}]: {before!r} -> {after!r}")
    print(json.dumps({"fields": len(changes), "by_table": totals}, ensure_ascii=False))


if __name__ == "__main__":
    main()
