#!/usr/bin/env python3
"""Verify a local capture export without displaying or uploading its text."""

import argparse
import hashlib
import json
from pathlib import Path
import re


LIMIT = 16 * 1024 * 1024


def verify(path: Path) -> dict:
    if not path.is_file():
        return {'state': 'unknown', 'complete': False, 'reason': 'status_file_missing'}
    if path.stat().st_size > 16384:
        raise ValueError('Capture status exceeds size limit')
    status = json.loads(path.read_text(encoding='utf-8'))
    if (not isinstance(status, dict) or type(status.get('schema_version')) is not int or
            status.get('schema_version') != 1):
        raise ValueError('Unsupported capture status')
    if (not isinstance(status.get('text_file'), str) or
            re.fullmatch(r'text-[0-9]+\.jsonl', status['text_file']) is None):
        raise ValueError('Invalid native session identity')
    for key in ('complete', 'stopped', 'truncated', 'write_failed', 'file_size_mismatch',
                'jsonl_may_be_incomplete'):
        if type(status.get(key)) is not bool:
            raise ValueError(f'Invalid status flag: {key}')
    for key in ('bytes_written', 'text_records', 'layout_records', 'dropped_records',
                'exported_text_bytes', 'exported_layout_bytes'):
        if type(status.get(key)) is not int or status[key] < 0:
            raise ValueError(f'Invalid status count: {key}')
    if not status['stopped']:
        raise ValueError('Capture is still active')
    total = status['exported_text_bytes'] + status['exported_layout_bytes']
    if total > LIMIT:
        raise ValueError('Capture files exceed combined size limit')
    counts = {}
    valid_jsonl = True
    files = []
    for kind in ('text', 'layout'):
        name = status.get(f'exported_{kind}_file')
        if (not isinstance(name, str) or not name or name in ('.', '..') or
                Path(name).name != name or '/' in name or '\\' in name):
            raise ValueError(f'Invalid {kind} filename')
        local = path.parent / name
        if local.is_symlink() or not local.is_file():
            raise ValueError(f'Missing or linked {kind} file')
        if local.stat().st_size != status[f'exported_{kind}_bytes']:
            raise ValueError(f'{kind} size mismatch')
        expected = status.get(f'{kind}_sha256')
        if not isinstance(expected, str) or re.fullmatch('[0-9a-f]{64}', expected) is None:
            raise ValueError(f'Invalid {kind} digest')
        data = local.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError(f'{kind} digest mismatch')
        files.append(local.resolve())
        rows = 0
        try:
            if data and not data.endswith(b'\n'):
                raise ValueError('Unterminated JSONL')
            # JSONL uses LF records. Unicode separators may occur inside a
            # valid JSON string and must not be treated as record boundaries.
            for line in data.split(b'\n')[:-1]:
                item = json.loads(line.decode('utf-8'))
                allowed = {'layout'} if kind == 'layout' else {'text', 'i18n'}
                if not isinstance(item, dict) or item.get('kind') not in allowed:
                    raise ValueError('Invalid record kind')
                rows += 1
        except (ValueError, UnicodeError):
            valid_jsonl = False
        counts[kind] = rows
    if files[0] == files[1]:
        raise ValueError('Text and layout must be different files')
    intact = (status['complete'] and not status['truncated'] and
              not status['write_failed'] and not status['file_size_mismatch'] and
              not status['jsonl_may_be_incomplete'] and
              status['dropped_records'] == 0 and total == status['bytes_written'] and
              valid_jsonl and counts['text'] == status['text_records'] and
              counts['layout'] == status['layout_records'])
    if status['complete'] and not intact:
        raise ValueError('Complete status contradicts files or counters')
    return {'state': 'complete' if intact else 'partial', 'complete': intact,
            'text_records': counts['text'], 'layout_records': counts['layout'],
            'bytes': total, 'jsonl_valid': valid_jsonl,
            'truncated': status['truncated'], 'write_failed': status['write_failed'],
            'dropped_records': status['dropped_records'],
            'scope': 'this_capture_only_not_all_game_text'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('status', type=Path, help='Exported .status.json beside both JSONL files')
    args = parser.parse_args()
    try:
        result = verify(args.status)
    except (ValueError, OSError, RecursionError):
        # Parsing exceptions can embed game text. Keep diagnostics structural.
        result = {'state': 'invalid', 'complete': False,
                  'reason': 'status_or_file_validation_failed'}
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result['complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
