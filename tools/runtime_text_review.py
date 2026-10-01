#!/usr/bin/env python3
"""Review captured runtime Text locally before adding it to translation inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys

from localize import ROOT, read_json, validate, write_json


DEFAULT_CANDIDATES = ROOT / ".analysis/runtime-text-candidates.json"
DEFAULT_REVIEW = ROOT / ".analysis/runtime-text-review.json"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_candidates(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if (not isinstance(data, dict) or data.get("audit_schema_version") != 2 or
            data.get("scope") != "local_review_only_no_automatic_translation" or
            not isinstance(data.get("texts"), list)):
        raise ValueError(f"{path}: invalid or outdated runtime candidate list")
    seen = set()
    for item in data["texts"]:
        if (not isinstance(item, dict) or not isinstance(item.get("source"), str) or
                not item["source"] or item["source"] in seen or
                item.get("category") != "still_unmatched" or
                not isinstance(item.get("bucket"), str) or
                item["bucket"].split("_", 1)[0] not in ("kana", "han") or
                type(item.get("occurrences")) is not int or item["occurrences"] < 1):
            raise ValueError(f"{path}: invalid runtime candidate entry")
        seen.add(item["source"])
    return data["texts"]


def prepare(candidate_path: Path, review_path: Path) -> int:
    candidates = read_candidates(candidate_path)
    if review_path.exists():
        raise ValueError(f"Review file already exists; preserve its decisions: {review_path}")
    if not review_path.resolve().is_relative_to((ROOT / ".analysis").resolve()):
        raise ValueError("Runtime review file must stay in the Git-ignored .analysis directory")
    write_json(review_path, {
        "scope": "runtime_text_human_review",
        "candidate_sha256": sha256(candidate_path),
        "entries": [{"source": item["source"],
                     "occurrences": item["occurrences"],
                     "decision": "skip", "translation": ""}
                    for item in candidates],
    })
    return len(candidates)


def validate_review_row(item: dict) -> None:
    if (not isinstance(item, dict) or not isinstance(item.get("source"), str) or
            not item["source"] or type(item.get("occurrences")) is not int or
            item["occurrences"] < 1 or
            item.get("decision") not in ("skip", "translate", "retain") or
            not isinstance(item.get("translation"), str)):
        raise ValueError("Runtime review contains an invalid decision or candidate")
    if item["decision"] in ("skip", "retain") and item["translation"]:
        raise ValueError("Skipped or retained runtime text must not contain a translation")
    if item["translation"]:
        validate(item["source"], item["translation"], "runtime_text")
        if (item["source"].count("\n") != item["translation"].count("\n") or
                item["source"].count(r"\n") != item["translation"].count(r"\n")):
            raise ValueError("Runtime Text translation changed line break structure")


def selected_entries(candidate_path: Path, review_path: Path) -> list[dict]:
    if not review_path.resolve().is_relative_to((ROOT / ".analysis").resolve()):
        raise ValueError("Runtime review file must stay in the Git-ignored .analysis directory")
    candidates = read_candidates(candidate_path)
    data = json.loads(review_path.read_text(encoding="utf-8"))
    if (not isinstance(data, dict) or data.get("scope") != "runtime_text_human_review" or
            data.get("candidate_sha256") != sha256(candidate_path) or
            not isinstance(data.get("entries"), list)):
        raise ValueError("Runtime review is invalid or stale; compare it with a fresh candidate list")
    expected = {item["source"]: item["occurrences"] for item in candidates}
    rows = data["entries"]
    if len(rows) != len(expected):
        raise ValueError("Runtime review omits or duplicates candidates")
    seen = set()
    selected = []
    for item in rows:
        validate_review_row(item)
        if (item["source"] in seen or item["source"] not in expected or
                expected[item["source"]] != item["occurrences"]):
            raise ValueError("Runtime review contains an invalid decision or candidate")
        seen.add(item["source"])
        if item["decision"] == "skip":
            continue
        selected.append(item)
    return selected


def refresh(candidate_path: Path, review_path: Path) -> dict[str, int | str]:
    """Carry decisions for identical sources; archive the old local review first."""
    if not review_path.resolve().is_relative_to((ROOT / ".analysis").resolve()):
        raise ValueError("Runtime review file must stay in the Git-ignored .analysis directory")
    candidate_hash = sha256(candidate_path)
    candidates = read_candidates(candidate_path)
    old_bytes = review_path.read_bytes()
    old_hash = hashlib.sha256(old_bytes).hexdigest()
    old = json.loads(old_bytes)
    if (not isinstance(old, dict) or old.get("scope") != "runtime_text_human_review" or
            not isinstance(old.get("candidate_sha256"), str) or
            not isinstance(old.get("entries"), list)):
        raise ValueError("Invalid old runtime review")
    if old["candidate_sha256"] == candidate_hash:
        selected_entries(candidate_path, review_path)
        return {"unchanged": len(candidates), "preserved_selected": 0,
                "new_candidates": 0, "removed_selected": 0}
    previous = {}
    for item in old["entries"]:
        validate_review_row(item)
        if item["source"] in previous:
            raise ValueError("Old runtime review contains duplicate sources")
        previous[item["source"]] = item
    rows = []
    preserved = new = 0
    current_sources = set()
    for item in candidates:
        source = item["source"]
        current_sources.add(source)
        old_row = previous.get(source)
        if old_row is None:
            new += 1
        elif old_row["decision"] != "skip":
            preserved += 1
        rows.append({"source": source, "occurrences": item["occurrences"],
                     "decision": old_row["decision"] if old_row else "skip",
                     "translation": old_row["translation"] if old_row else ""})
    removed = sum(item["decision"] != "skip" for source, item in previous.items()
                  if source not in current_sources)
    if sha256(candidate_path) != candidate_hash or sha256(review_path) != old_hash:
        raise ValueError("Candidate or review file changed during refresh; retry")
    archive_dir = review_path.parent / "review-archive"
    archive_dir.mkdir(parents=True, exist_ok=True)
    if not archive_dir.resolve().is_relative_to((ROOT / ".analysis").resolve()):
        raise ValueError("Runtime review archive must stay in the Git-ignored .analysis directory")
    archive = archive_dir / f"{old_hash}.json"
    if archive.exists():
        if archive.read_bytes() != old_bytes:
            raise ValueError(f"Review archive hash collision: {archive}")
    else:
        with archive.open("xb") as stream:
            stream.write(old_bytes)
    write_json(review_path, {"scope": "runtime_text_human_review",
                             "candidate_sha256": candidate_hash, "entries": rows})
    return {"candidates": len(rows), "preserved_selected": preserved,
            "new_candidates": new, "removed_selected": removed,
            "archive": str(archive)}


def apply_review(candidate_path: Path, review_path: Path,
                 source_path: Path, translation_path: Path) -> dict[str, int]:
    selected = selected_entries(candidate_path, review_path)
    source = read_json(source_path)
    translated = read_json(translation_path)
    source_text = source.setdefault("text", {})
    translated_text = translated.setdefault("text", {})
    added_sources = added_translations = preserved = 0
    for item in selected:
        original = item["source"]
        current_source = source_text.get(original)
        if current_source is not None and current_source != original:
            raise ValueError(f"Runtime source conflicts with existing source entry: {original!r}")
        if current_source is None:
            source_text[original] = original
            added_sources += 1
        target = original if item["decision"] == "retain" else item["translation"]
        if not target:
            continue
        if original in translated_text:
            preserved += 1
        else:
            translated_text[original] = target
            added_translations += 1
    if added_sources:
        write_json(source_path, source)
    if added_translations:
        write_json(translation_path, translated)
    return {"selected": len(selected), "added_sources": added_sources,
            "added_translations": added_translations, "preserved_translations": preserved}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("prepare", "refresh", "status", "apply"))
    parser.add_argument("--candidates", type=Path, default=DEFAULT_CANDIDATES)
    parser.add_argument("--review", type=Path, default=DEFAULT_REVIEW)
    parser.add_argument("--source", type=Path, default=ROOT / "translations/ui/source.json")
    parser.add_argument("--translations", type=Path,
                        default=ROOT / "translations/ui/zh-Hans.json")
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            result = {"prepared": prepare(args.candidates, args.review)}
        elif args.command == "refresh":
            result = refresh(args.candidates, args.review)
        elif args.command == "status":
            selected = selected_entries(args.candidates, args.review)
            result = {"selected": len(selected),
                      "pending_api": sum(item["decision"] == "translate" and
                                         not item["translation"] for item in selected),
                      "manual": sum(bool(item["translation"]) for item in selected),
                      "retained": sum(item["decision"] == "retain" for item in selected)}
        else:
            result = apply_review(args.candidates, args.review,
                                  args.source, args.translations)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"Runtime text review failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
