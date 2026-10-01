#!/usr/bin/env python3
"""Inspect selected text controls in a local layout capture; never upload it."""
import argparse
import json
import math
from pathlib import Path


def inspect(path: Path, texts: set[str]) -> list[dict]:
    if not texts or path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError('Select exact visible text; capture limit is 16 MiB')
    matches = {}
    with path.open(encoding='utf-8') as stream:
        for number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            row = json.loads(line)
            if not isinstance(row, dict) or row.get('kind') != 'layout':
                raise ValueError(f'Line {number}: expected a layout record')
            if row.get('text') not in texts:
                continue
            if not isinstance(row.get('object_path'), str) or not row['object_path']:
                raise ValueError(f'Line {number}: missing control path')
            if type(row.get('alignment')) is not int:
                raise ValueError(f'Line {number}: invalid TMP alignment')
            for field, count in (('rect', 4), ('anchored_position', 2), ('font_size', 1)):
                values = [row.get(field)] if count == 1 else row.get(field)
                if (not isinstance(values, list) or len(values) != count or
                        any(type(v) not in (int, float) or not math.isfinite(v) for v in values)):
                    raise ValueError(f'Line {number}: invalid {field}')
            selected = {key: row[key] for key in ('text', 'object_path', 'font_size',
                                                 'alignment', 'rect', 'anchored_position')}
            identity = json.dumps(selected, ensure_ascii=False, sort_keys=True)
            if identity not in matches:
                matches[identity] = {**selected, 'first_line': number, 'observations': 0}
            matches[identity]['observations'] += 1
    return sorted(matches.values(), key=lambda row: (row['object_path'], row['first_line']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path, help='Exported .layout.jsonl file')
    parser.add_argument('--text', action='append', required=True, help='Exact displayed text; repeatable')
    args = parser.parse_args()
    print(json.dumps(inspect(args.capture, set(args.text)), ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
