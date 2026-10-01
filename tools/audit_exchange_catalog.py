#!/usr/bin/env python3
"""Audit official exchange snapshot text against the compiled runtime dictionary."""
from __future__ import annotations

from collections import Counter
import argparse
import hashlib
import json
import re
from pathlib import Path

try:
    from .audit_shop_catalog import audit_catalog
except ImportError:
    from audit_shop_catalog import audit_catalog


def _scoped_reset_description(source: str, text: dict) -> bool:
    """Match the native Description grammar, including its byte-size limit."""
    marker = '\n在庫リセット\n'
    if len(source.encode('utf-8')) > 4096 or source.count(marker) != 1:
        return False
    base, date = source.split(marker)
    if not base or not text.get(base) or not text.get('在庫リセット'):
        return False
    match = re.fullmatch(r'([0-9]{1,2})/([0-9]{1,2})\(([日月火水木金土])\) ([0-9]{2}):([0-9]{2})', date)
    if not match:
        return False
    month, day, _, hour, minute = match.groups()
    month, day, hour, minute = map(int, (month, day, hour, minute))
    days = (31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
    return 1 <= month <= 12 and 1 <= day <= days[month-1] and hour <= 23 and minute <= 59


def audit_exchange_catalog(catalog: dict, dictionary: dict) -> tuple[dict, dict]:
    if not isinstance(catalog, dict):
        raise ValueError('exchange catalog must be an object')
    # Reuse coverage and deduplication, projecting only the exchange text
    # whitelist. Resource/reward IDs do not imply runtime translation coverage.
    booths = catalog.get('booths')
    rows = None
    if isinstance(booths, list):
        rows = []
        for booth in booths:
            if not isinstance(booth, dict):
                rows.append(None)
                continue
            exchanges = booth.get('exchanges')
            items = None
            if isinstance(exchanges, list):
                items = [{'id': item.get('id'), 'name': item.get('name'),
                          'description': item.get('description')}
                         if isinstance(item, dict) else None for item in exchanges]
            rows.append({'id': booth.get('id'), 'name': booth.get('name'),
                         'status': booth.get('status'), 'items': items})
    report, pending = audit_catalog({'schema_version': catalog.get('schema_version', catalog.get('schema')),
                                     'complete': catalog.get('complete'), 'shops': rows,
                                     'dokanInfos': []}, dictionary)
    for record in report['records']:
        record['identity'][0] = 'exchange'
        # The exact exchange detail Name route supports reviewed base × quantity.
        # Description and booth labels do not use this runtime matcher.
        source = record['source']
        if (record['coverage'] == 'pending' and len(record['identity']) == 5
                and record['identity'][-1] == 'name' and len(source.encode('utf-8')) <= 1024
                and source.count('×') == 1):
            base, quantity = source.split('×')
            target = dictionary.get('text', {}).get(base)
            if (base and re.fullmatch(r'(?:[0-9]{1,6}|[1-9][0-9]{0,2}(?:,[0-9]{3}){1,2})', quantity)
                    and isinstance(target, str) and target):
                record['coverage'] = 'scoped_quantity'
        if (record['coverage'] == 'pending' and len(record['identity']) == 5
                and record['identity'][-1] == 'description'
                and _scoped_reset_description(source, dictionary.get('text', {}))):
            record['coverage'] = 'scoped_reset_description'
    rename = {'invalid_shops': 'invalid_booths', 'invalid_shop': 'invalid_booth',
              'shop_not_complete': 'booth_not_complete', 'invalid_items': 'invalid_exchanges',
              'invalid_item': 'invalid_exchange'}
    for issue in report['issues']:
        issue['kind'] = rename.get(issue['kind'], issue['kind'])
        if issue.get('identity') and issue['identity'][0] == 'shop':
            issue['identity'][0] = 'exchange'
    conflicts = catalog.get('conflicts', [])
    if not isinstance(conflicts, list):
        report['issues'].append({'kind': 'invalid_collector_conflicts'})
    else:
        for conflict in conflicts:
            # Only expose declared official identities and field names, never
            # arbitrary collector payloads or error/account data.
            if not isinstance(conflict, dict):
                report['issues'].append({'kind': 'invalid_collector_conflict'})
                continue
            booth_id, item_id, field = (conflict.get('booth_id'), conflict.get('exchange_id'), conflict.get('field'))
            valid_id = lambda value: not isinstance(value, bool) and isinstance(value, (str, int)) and bool(str(value))
            if not valid_id(booth_id) or field not in ('name', 'description') or (item_id is not None and not valid_id(item_id)):
                report['issues'].append({'kind': 'invalid_collector_conflict'})
                continue
            identity = ['exchange', str(booth_id)]
            if item_id is not None:
                identity += ['item', str(item_id)]
            identity.append(field)
            report['issues'].append({'kind': 'collector_identity_conflict', 'identity': identity})
    report['issues'] = [json.loads(x) for x in sorted({json.dumps(x, sort_keys=True) for x in report['issues']})]
    report['complete'] = report['complete'] and not report['issues']
    sources = sorted({r['source'] for r in report['records'] if r['coverage'] == 'pending'})
    pending = {'text': {source: source for source in sources}}
    report['distinct_pending_sources'] = len(sources)
    report['counts'] = dict(sorted(Counter(r['coverage'] for r in report['records']).items()))
    report['coverage_complete'] = report['complete'] and not sources
    report['booth_count'] = report.pop('shop_count')
    report['exchange_item_count'] = len({tuple(r['identity'][:-1]) for r in report['records']
                                       if len(r['identity']) == 5})
    report['scope'] = 'official_exchange_text_whitelist'
    return report, pending


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--dictionary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pending', type=Path, required=True)
    args = parser.parse_args(argv)
    report, pending = audit_exchange_catalog(json.loads(args.catalog.read_text(encoding='utf-8')),
                                            json.loads(args.dictionary.read_text(encoding='utf-8')))
    report['catalog_sha256'] = hashlib.sha256(args.catalog.read_bytes()).hexdigest()
    report['dictionary_sha256'] = hashlib.sha256(args.dictionary.read_bytes()).hexdigest()
    for path, value in ((args.output, report), (args.pending, pending)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    return (0 if report['coverage_complete'] else 1) if report['complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
