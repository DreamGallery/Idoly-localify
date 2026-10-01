#!/usr/bin/env python3
"""Compare reviewed MasterDB fields with the final source-string Text dictionary.

This predicts what the Text hook would return for an exact source string; it
does not prove that the game actually routes each field through that hook.
"""

import argparse
from collections import Counter
import json
from pathlib import Path

from localize import write_json
from master_json import ORIG, ROOT, ZH, iter_fields


def audit(orig_dir: Path, zh_dir: Path, dictionary: Path, *, details: bool = False) -> dict:
    data = json.loads(dictionary.read_text(encoding="utf-8"))
    mapping = data.get("text") if isinstance(data, dict) else None
    if not isinstance(mapping, dict) or any(not isinstance(k, str) or
                                            not isinstance(v, str)
                                            for k, v in mapping.items()):
        raise ValueError(f"{dictionary}: expected a string-to-string text dictionary")
    counts = Counter()
    by_table = {}
    mismatches = []
    for table, record_id, field_path, source, target in iter_fields(orig_dir, zh_dir):
        if not target:
            kind = "untranslated"
        elif mapping.get(source, source) == target:
            kind = "matching"
        elif target == source:
            kind = "retained_changed"
        else:
            kind = "translated_differently"
        counts[kind] += 1
        by_table.setdefault(table, Counter())[kind] += 1
        if details and kind in ("retained_changed", "translated_differently"):
            mismatches.append({"table": table, "record_id": record_id,
                               "field_path": field_path, "source": source,
                               "reviewed": target, "runtime_text": mapping.get(source, source),
                               "kind": kind})
    result = {"scope": "exact source-string Text hook prediction",
              "counts": dict(sorted(counts.items())),
              "by_table": {name: dict(sorted(values.items()))
                           for name, values in sorted(by_table.items())}}
    if details:
        result["mismatches"] = mismatches
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--orig-dir", type=Path, default=ORIG)
    parser.add_argument("--zh-dir", type=Path, default=ZH)
    parser.add_argument("--dictionary", type=Path,
                        default=ROOT / "app/src/main/assets/idoly-localify/dict.json")
    parser.add_argument("--output", type=Path,
                        default=ROOT / ".analysis/master-runtime-audit.json")
    parser.add_argument("--details", action="store_true",
                        help="Include raw mismatching text; output must stay in .analysis")
    args = parser.parse_args()
    if args.details and args.output.resolve() != (ROOT / ".analysis").resolve() and \
            (ROOT / ".analysis").resolve() not in args.output.resolve().parents:
        parser.error("--details output must stay in the Git-ignored .analysis directory")
    result = audit(args.orig_dir, args.zh_dir, args.dictionary, details=args.details)
    write_json(args.output, result)
    print(json.dumps(result["counts"], ensure_ascii=False, sort_keys=True))


if __name__ == "__main__":
    main()
