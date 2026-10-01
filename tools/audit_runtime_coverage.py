#!/usr/bin/env python3
"""Recheck historical unmatched game strings against the current text dictionary.

The native dump contains only strings that missed hooks at capture time. Its
counts are not a translation coverage denominator for the whole game.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sys

from localize import ROOT, captured_entries, write_json
from numeric_text_templates import compile_templates, matches_any
from home_activity_names import compile_names, may_match


KANA = re.compile(r"[\u3040-\u30ff]")
HAN = re.compile(r"[\u3400-\u9fff]")
DIGIT = re.compile(r"[0-9０-９]")
ELAPSED_SOURCE = re.compile(r"[0-9]+(?:日前|時間前|分前)\Z")
ELAPSED_RENDERED = re.compile(r"[0-9]+(?:天前|小时前|分钟前)\Z")


def unmatched_bucket(source: str) -> str:
    family = "kana" if KANA.search(source) else (
        "han" if HAN.search(source) else "latin_or_symbol")
    return family + ("_with_digit" if DIGIT.search(source) else "_without_digit")


def object_file(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected JSON object")
    return value


def file_digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def directory_digest(directory: Path) -> str:
    value = hashlib.sha256()
    files = sorted(directory.glob("*.json"))
    if not files:
        raise ValueError(f"No MasterDB source JSON files in {directory}")
    for path in files:
        value.update(path.name.encode("utf-8"))
        value.update(b"\0")
        value.update(bytes.fromhex(file_digest(path)))
    return value.hexdigest()


def textual_candidates(report: dict) -> list[dict]:
    return [item for item in report["details"]["text"]
            if item["category"] == "still_unmatched" and
            item["bucket"].split("_", 1)[0] in ("kana", "han")]


def master_sources(directory: Path) -> set[str]:
    files = sorted(directory.glob("*.json"))
    if not files:
        raise ValueError(f"No MasterDB source JSON files in {directory}")
    values = set()
    for path in files:
        table = object_file(path)
        for record in table.values():
            if not isinstance(record, dict) or any(not isinstance(value, str)
                                                    for value in record.values()):
                raise ValueError(f"{path}: invalid MasterDB source record")
            values.update(record.values())
    return values


def audit(dump_path: Path, dictionary_path: Path, master_dir: Path,
          ui_source_path: Path | None = None, ui_translation_path: Path | None = None,
          *, include_details: bool = False) -> dict:
    dictionary = object_file(dictionary_path)
    for name in ("text", "i18n", "i18n_by_source"):
        if not isinstance(dictionary.get(name), dict):
            raise ValueError(f"{dictionary_path}: invalid {name} dictionary")
    if (any(not isinstance(key, str) or not isinstance(value, str)
            for name in ("text", "i18n")
            for key, value in dictionary[name].items()) or
            any(not isinstance(key, str) or not isinstance(values, dict) or
                any(not isinstance(source, str) or not isinstance(target, str)
                    for source, target in values.items())
                for key, values in dictionary["i18n_by_source"].items())):
        raise ValueError(f"{dictionary_path}: invalid dictionary strings")
    if not isinstance(dictionary.get("translated_values"), list) or any(
            not isinstance(value, str) for value in dictionary["translated_values"]):
        raise ValueError(f"{dictionary_path}: invalid translated_values")
    numeric_templates = dictionary.get("numeric_text_templates", {})
    if not isinstance(numeric_templates, dict):
        raise ValueError(f"{dictionary_path}: invalid numeric_text_templates")
    compile_templates({key: key for key in numeric_templates}, numeric_templates)
    activity_names = dictionary.get("home_activity_names", {})
    if not isinstance(activity_names, dict):
        raise ValueError(f"{dictionary_path}: invalid home_activity_names")
    compile_names({key: key for key in activity_names}, activity_names)
    static_sources = master_sources(master_dir)
    ui_source = object_file(ui_source_path).get("i18n", {}) if ui_source_path else {}
    ui_translation = object_file(ui_translation_path) if ui_translation_path else {}
    stable_reviewed = ui_translation.get("i18n", {})
    variant_reviewed = ui_translation.get("i18n_by_source", {})
    if (not isinstance(ui_source, dict) or not isinstance(stable_reviewed, dict) or
            not isinstance(variant_reviewed, dict) or
            any(not isinstance(values, dict) for values in variant_reviewed.values())):
        raise ValueError("Invalid reviewed UI source or translation dictionary")
    text_counts = Counter()
    i18n_counts = Counter()
    records = 0
    recovered = 0
    for _, kind, key, source, nul_recovered in captured_entries(dump_path):
        records += 1
        recovered += nul_recovered
        if kind == "text":
            text_counts[source] += 1
        else:
            i18n_counts[(key, source)] += 1

    translated_values = set(dictionary["translated_values"])
    translated_values.update(dictionary["text"].values())
    translated_values.update(dictionary["i18n"].values())
    translated_values.update(value for variants in dictionary["i18n_by_source"].values()
                             for value in variants.values())
    compiled_text = dictionary["text"]
    text_categories = Counter()
    text_traits = Counter()
    text_buckets = Counter()
    text_details = []
    for source, count in text_counts.items():
        if source in compiled_text and compiled_text[source] != source:
            category = "now_exact_text"
        elif source in translated_values:
            category = "now_rendered_translation"
        elif matches_any(source, numeric_templates):
            category = "now_numeric_template_source"
        elif matches_any(source, numeric_templates, rendered=True):
            category = "now_rendered_numeric_template"
        elif ELAPSED_SOURCE.fullmatch(source):
            category = "now_dynamic_elapsed_source"
        elif ELAPSED_RENDERED.fullmatch(source):
            category = "now_rendered_dynamic_elapsed"
        elif source in static_sources:
            category = "master_source_without_change"
        else:
            category = "still_unmatched"
        text_categories[category] += 1
        if category == "still_unmatched":
            text_buckets[unmatched_bucket(source)] += 1
            text_traits["contains_kana"] += bool(KANA.search(source))
            text_traits["contains_digit"] += bool(DIGIT.search(source))
            text_traits["contains_newline"] += "\n" in source
        if include_details and category in ("still_unmatched", "master_source_without_change"):
            detail = {"source": source, "occurrences": count,
                      "category": category}
            if category == "still_unmatched":
                detail["bucket"] = unmatched_bucket(source)
                if may_match(source, activity_names):
                    # Text-only dumps cannot prove the required control path.
                    detail["path_scoped_candidate"] = "home_activity_names"
            text_details.append(detail)

    i18n_categories = Counter()
    i18n_details = []
    for (key, source), count in i18n_counts.items():
        if source in dictionary["i18n_by_source"].get(key, {}):
            category = "now_source_variant"
        elif key in dictionary["i18n"]:
            category = "now_stable_key"
        elif source in compiled_text and compiled_text[source] != source:
            category = "possible_text_fallback"
        elif variant_reviewed.get(key, {}).get(source) == source:
            category = "reviewed_retained_variant"
        elif stable_reviewed.get(key) == source:
            category = "reviewed_retained_key"
        elif ui_source.get(key) == source:
            category = "known_untranslated_key"
        elif key in ui_source:
            category = "unreviewed_source_variant"
        else:
            category = "unknown_key"
        i18n_categories[category] += 1
        if include_details and category in ("known_untranslated_key",
                                           "unreviewed_source_variant", "unknown_key"):
            i18n_details.append({"key": key, "source": source,
                                 "occurrences": count})

    result = {
        "scope": "historical_unmatched_dump_rechecked_against_current_dictionary",
        "capture_records": records,
        "nul_recovered_records": recovered,
        "unique_text": len(text_counts),
        "unique_i18n_key_source": len(i18n_counts),
        "text_categories": dict(sorted(text_categories.items())),
        "still_unmatched_text_buckets": dict(sorted(text_buckets.items())),
        "still_unmatched_text_traits": dict(sorted(text_traits.items())),
        "i18n_categories": dict(sorted(i18n_categories.items())),
    }
    if include_details:
        result["details"] = {
            "text": sorted(text_details, key=lambda item: (-item["occurrences"], item["source"])),
            "i18n": sorted(i18n_details, key=lambda item: (-item["occurrences"], item["key"])),
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="local unmatched dump.jsonl")
    parser.add_argument("--dictionary", type=Path,
                        default=ROOT / "app/src/main/assets/idoly-localify/dict.json")
    parser.add_argument("--master-orig", type=Path,
                        default=ROOT / "translations/master/orig")
    parser.add_argument("--ui-source", type=Path,
                        default=ROOT / "translations/ui/source.json")
    parser.add_argument("--ui-translation", type=Path,
                        default=ROOT / "translations/ui/zh-Hans.json")
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/runtime-gap-audit.json")
    parser.add_argument("--details", action="store_true",
                        help="Include raw candidate strings in the local, Git-ignored report")
    parser.add_argument("--candidate-output", type=Path,
                        help="Write only unmatched kana/Han text to a local review list")
    args = parser.parse_args()
    if args.details and not args.output.resolve().is_relative_to((ROOT / ".analysis").resolve()):
        parser.error("--details output must remain inside the Git-ignored .analysis directory")
    if args.candidate_output and not args.candidate_output.resolve().is_relative_to(
            (ROOT / ".analysis").resolve()):
        parser.error("--candidate-output must remain inside the Git-ignored .analysis directory")
    if args.candidate_output and args.candidate_output.resolve() == args.output.resolve():
        parser.error("--candidate-output and --output must be different files")
    try:
        result = audit(args.dump, args.dictionary, args.master_orig,
                       args.ui_source, args.ui_translation,
                       include_details=args.details or bool(args.candidate_output))
        if args.candidate_output:
            candidates = textual_candidates(result)
            write_json(args.candidate_output, {
                "scope": "local_review_only_no_automatic_translation",
                "audit_schema_version": 2,
                "dump_sha256": file_digest(args.dump),
                "dictionary_sha256": file_digest(args.dictionary),
                "master_orig_sha256": directory_digest(args.master_orig),
                "ui_source_sha256": file_digest(args.ui_source),
                "ui_translation_sha256": file_digest(args.ui_translation),
                "texts": candidates,
            })
            result["local_review_candidates"] = len(candidates)
        if not args.details:
            result.pop("details", None)
        write_json(args.output, result)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Runtime audit failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps({key: value for key, value in result.items() if key != "details"},
                     ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
