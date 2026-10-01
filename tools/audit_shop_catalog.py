#!/usr/bin/env python3
"""Offline audit of whitelisted official shop text against a compiled dictionary."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

try:
    from .numeric_text_templates import matches_any, split_template
except ImportError:
    from numeric_text_templates import matches_any, split_template

CJK_RANGES = ((0x3041, 0x3096), (0x3099, 0x309f), (0x30a1, 0x30fa),
              (0x30fc, 0x30ff), (0x31f0, 0x31ff), (0x3400, 0x4dbf),
              (0x4e00, 0x9fff), (0xf900, 0xfaff), (0xff66, 0xff9f),
              (0x1aff0, 0x1afff), (0x1b000, 0x1b16f),
              (0x20000, 0x2fa1f), (0x30000, 0x3347f))
SUCCESS_STATUSES = {'ok', 'success', 'complete'}


def has_cjk(text: str) -> bool:
    return any(ord(c) in (0x3005, 0x3007, 0x303b) or
               any(lo <= ord(c) <= hi for lo, hi in CJK_RANGES) for c in text)


def audit_catalog(catalog: dict, dictionary: dict) -> tuple[dict, dict]:
    if not isinstance(catalog, dict) or not isinstance(dictionary, dict):
        raise ValueError('catalog and compiled dictionary must be objects')
    exact = dictionary.get('text', {})
    numeric = dictionary.get('numeric_text_templates', {})
    for label, table in (('text', exact), ('numeric_text_templates', numeric)):
        if not isinstance(table, dict) or any(not isinstance(k, str) or
                not isinstance(v, str) or not v for k, v in table.items()):
            raise ValueError(f'invalid compiled dictionary {label}')
    for source, target in numeric.items():
        split_template(source, source=True)
        split_template(target, source=False)

    issues = []
    values = {}
    shops = {}
    schema = catalog.get('schema_version', catalog.get('schema'))
    if schema != 1 or isinstance(schema, bool):
        issues.append({'kind': 'invalid_schema'})

    def identity_id(node, parent):
        value = node.get('id')
        if isinstance(value, bool) or not isinstance(value, (str, int)) or not str(value):
            issues.append({'kind': 'invalid_id', 'identity': list(parent)})
            return None
        return str(value)

    def fields(node, identity, names):
        for field in names:
            source = node.get(field, '')
            if source is None:
                source = ''
            if not isinstance(source, str):
                issues.append({'kind': 'invalid_field', 'identity': [*identity, field]})
                continue
            key = (*identity, field)
            values.setdefault(key, set()).add(source)

    rows = catalog.get('shops')
    if not isinstance(rows, list):
        issues.append({'kind': 'invalid_shops'})
        rows = []
    for shop in rows:
        if not isinstance(shop, dict):
            issues.append({'kind': 'invalid_shop'})
            continue
        shop_id = identity_id(shop, ('shop',))
        if shop_id is None:
            continue
        identity = ('shop', shop_id)
        status = shop.get('status')
        ok = isinstance(status, str) and status in SUCCESS_STATUSES
        shops.setdefault(shop_id, set()).add(ok)
        if not ok:
            issues.append({'kind': 'shop_not_complete', 'identity': list(identity)})
        fields(shop, identity, ('name',))
        items = shop.get('items')
        if not isinstance(items, list):
            issues.append({'kind': 'invalid_items', 'identity': list(identity)})
            continue
        for item in items:
            if not isinstance(item, dict):
                issues.append({'kind': 'invalid_item', 'identity': list(identity)})
                continue
            item_id = identity_id(item, (*identity, 'item'))
            if item_id is not None:
                fields(item, (*identity, 'item', item_id), ('name', 'description'))

    dokan = catalog.get('dokanInfos')
    if not isinstance(dokan, list):
        issues.append({'kind': 'invalid_dokan_infos'})
        dokan = []
    for row in dokan:
        if not isinstance(row, dict):
            issues.append({'kind': 'invalid_dokan'})
            continue
        row_id = identity_id(row, ('dokan',))
        if row_id is None:
            continue
        identity = ('dokan', row_id)
        fields(row, identity, ('name', 'description', 'linkTitle'))
        item = row.get('shopItem')
        if isinstance(item, dict):
            item_id = identity_id(item, (*identity, 'shopItem'))
            if item_id is not None:
                fields(item, (*identity, 'shopItem', item_id), ('name', 'description'))

    records = []
    pending = set()
    for identity, sources in sorted(values.items()):
        conflict = len(sources) > 1
        if conflict:
            issues.append({'kind': 'identity_source_conflict', 'identity': list(identity)})
        for source in sorted(sources):
            if not source:
                coverage = 'empty'
            elif source in exact:
                coverage = 'exact_text'
            elif matches_any(source, numeric):
                coverage = 'numeric_template'
            elif not has_cjk(source):
                coverage = 'non_cjk'
            else:
                coverage = 'pending'
                pending.add(source)
            records.append({'identity': list(identity), 'source': source,
                            'source_sha': hashlib.sha256(source.encode('utf-8')).hexdigest(),
                            'coverage': coverage, 'identity_conflict': conflict})
    # Stable and deduplicated diagnostics; never propagate request bodies,
    # account fields, unknown item properties, or collector error messages.
    issues = [json.loads(x) for x in sorted({json.dumps(x, sort_keys=True) for x in issues})]
    collector_complete = catalog.get('complete') is True
    report = {'schema_version': 1, 'scope': 'current_account_api_returned_directory',
              'collector_complete': collector_complete,
              'complete': collector_complete and not issues,
              'coverage_complete': not pending and collector_complete and not issues,
              'shop_count': len(shops), 'field_count': len(records),
              'distinct_pending_sources': len(pending),
              'counts': dict(sorted(Counter(r['coverage'] for r in records).items())),
              'issues': issues, 'records': records}
    return report, {'text': {source: source for source in sorted(pending)}}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--dictionary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pending', type=Path, required=True)
    args = parser.parse_args(argv)
    report, pending = audit_catalog(json.loads(args.catalog.read_text(encoding='utf-8')),
                                   json.loads(args.dictionary.read_text(encoding='utf-8')))
    report['catalog_sha256'] = hashlib.sha256(args.catalog.read_bytes()).hexdigest()
    report['dictionary_sha256'] = hashlib.sha256(args.dictionary.read_bytes()).hexdigest()
    for path, value in ((args.output, report), (args.pending, pending)):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return (0 if report['coverage_complete'] else 1) if report['complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
