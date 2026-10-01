"""Derive limited Name×quantity candidates from explicitly approved Master Item IDs."""

import argparse
import hashlib
import json
from pathlib import Path

from numeric_text_templates import compile_templates


def derive_item_templates(original, translated, approved_ids):
    templates, evidence = {}, []
    for item_id in approved_ids:
        if item_id not in original or item_id not in translated:
            raise ValueError(f'Approved Item ID absent from a Master input: {item_id}')
        source, target = original[item_id].get('name'), translated[item_id].get('name')
        if not isinstance(source, str) or not source or not isinstance(target, str) or not target:
            raise ValueError('Approved Item requires nonempty source and reviewed name')
        key, value = source + '×{number}', target + '×{number}'
        if key in templates and templates[key] != value:
            raise ValueError('Conflicting reviewed translations for the same Item name')
        templates[key] = value
        evidence.append({'table': 'Item', 'id': item_id, 'field': 'name',
                         'source': source, 'translation': target})
    compile_templates({key: key for key in templates}, templates)
    return templates, evidence


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--master-orig', required=True, type=Path)
    parser.add_argument('--master-zh', required=True, type=Path)
    parser.add_argument('--approved-ids', required=True, type=Path,
                        help='JSON object with approved_item_ids; only these Item records are used')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args(argv)
    inputs = [args.master_orig, args.master_zh, args.approved_ids]
    if args.output.resolve() in {path.resolve() for path in inputs}:
        parser.error('Output must differ from inputs')
    orig, zh, config = [json.loads(path.read_text(encoding='utf-8')) for path in inputs]
    approved = config.get('approved_item_ids')
    if not isinstance(approved, list) or any(not isinstance(item, str) for item in approved):
        raise ValueError('approved_item_ids must be an explicit list')
    templates, evidence = derive_item_templates(orig, zh, approved)
    report = {'schema_version': 1, 'scope': 'approved_item_quantity_candidates_no_dictionary_changes',
              'inputs': [{'source_file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                         for path in inputs],
              'templates': templates, 'evidence': evidence,
              'summary': {'approved_item_ids': len(approved), 'candidate_templates': len(templates)}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report['summary']))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
