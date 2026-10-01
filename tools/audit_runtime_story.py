#!/usr/bin/env python3
"""Match captured TMP text to adventure translations changed by game rendering."""

from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import sys

from localize import write_json

ROOT = Path(__file__).resolve().parents[1]


def captured_text(path: Path) -> set[str]:
    result = set()
    with path.open(encoding="utf-8") as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                item = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"{path}:{number}: invalid JSON: {error}") from error
            if not isinstance(item, dict) or item.get("kind") not in ("text", "i18n"):
                raise ValueError(f"{path}:{number}: invalid capture entry")
            if item["kind"] != "text":
                continue
            source = item.get("source")
            if not isinstance(source, str):
                raise ValueError(f"{path}:{number}: text source must be a string")
            if source:
                result.add(source)
    return result


def story_candidates(story_dir: Path) -> dict[str, list[dict[str, str]]]:
    candidates = defaultdict(list)
    for path in sorted(story_dir.glob("adv_*.json")):
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict) or not isinstance(data.get("translations"), dict):
            raise ValueError(f"{path}: invalid story patch")
        for identifier, translated in data["translations"].items():
            if not isinstance(identifier, str) or not isinstance(translated, str):
                raise ValueError(f"{path}: invalid story translation")
            if not (translated.startswith("（") and translated.endswith("）")):
                continue
            inner = translated[1:-1]
            if not inner:
                continue
            location = {"file": path.name, "field": identifier}
            candidates[inner].append(location)
            if r"\n" in inner:
                candidates[inner.replace(r"\n", "\n")].append(location)
    return candidates


def audit(dump_path: Path, story_dir: Path) -> dict:
    observed = captured_text(dump_path)
    candidates = story_candidates(story_dir)
    matches = [{"rendered": source, "locations": candidates[source]}
               for source in sorted(observed & candidates.keys())]
    return {"observed_text": len(observed),
            "parenthesized_candidates": len(candidates),
            "matched_rendered_text": len(matches),
            "matches": matches}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dump", type=Path, help="local game dump.jsonl")
    parser.add_argument("--story-dir", type=Path, default=ROOT / "translations/story")
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/story-rendering-matches.json")
    args = parser.parse_args()
    try:
        report = audit(args.dump, args.story_dir)
        write_json(args.output, report)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"{report['matched_rendered_text']} of {report['observed_text']} captured text "
          f"values match parenthesized story translations; report: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
