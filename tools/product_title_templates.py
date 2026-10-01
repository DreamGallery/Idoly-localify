"""Derive full-string serial-number templates from reviewed product titles only."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile

from numeric_text_templates import compile_templates


NUMBERED_TITLE = re.compile(r'(.+?)([0-9]{1,6})\Z', re.ASCII)


def derive_templates(entries, approved_stems, *, confirmed_single_stems=()):
    """Require identical translations for at least two serials, unless explicitly confirmed.

    Callers must supply only reviewed product-title pairs and an explicit stem
    allowlist. This does not scan arbitrary UI strings or player text.
    """
    approved_stems = set(approved_stems)
    confirmed_single_stems = set(confirmed_single_stems)
    if not confirmed_single_stems <= approved_stems:
        raise ValueError('Single-example confirmations must belong to the approved stems')
    grouped = {}
    for entry in entries:
        source, target = entry['source'], entry['translation']
        match = NUMBERED_TITLE.fullmatch(source)
        if not match:
            continue
        stem, number = match.groups()
        if stem not in approved_stems:
            continue
        # Do not turn the last six digits of a longer number into a template.
        if stem[-1:].isdigit():
            raise ValueError('Product serial number must contain at most six ASCII digits')
        translated = NUMBERED_TITLE.fullmatch(target)
        if not translated or translated.group(2) != number:
            raise ValueError('Reviewed product translation must preserve the serial number')
        target_stem = translated.group(1)
        group = grouped.setdefault(stem, {'target_stem': target_stem, 'numbers': set()})
        if group['target_stem'] != target_stem:
            raise ValueError('Conflicting translations for the same product-title stem')
        group['numbers'].add(number)
    templates = {}
    pending = []
    for stem, group in sorted(grouped.items()):
        if len(group['numbers']) < 2 and stem not in confirmed_single_stems:
            pending.append(stem)
            continue
        templates[stem + '{number}'] = group['target_stem'] + '{number}'
    compile_templates({source: source for source in templates}, templates)
    return templates, pending


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--titles', required=True, type=Path,
                        help='Reviewed product-title JSON with entries/source/translation')
    parser.add_argument('--config', required=True, type=Path,
                        help='JSON with approved_stems and optional confirmed_single_stems')
    parser.add_argument('--output', required=True, type=Path,
                        help='Candidate report only; no formal dictionary is modified')
    args = parser.parse_args(argv)
    if args.output.resolve() in {args.titles.resolve(), args.config.resolve()}:
        parser.error('Output must differ from input files')
    titles = json.loads(args.titles.read_text(encoding='utf-8'))
    if not isinstance(titles, dict) or titles.get('kind') not in (
            'reviewed_product_titles', 'shop_detail_title_model_review'):
        raise ValueError('Input must explicitly contain reviewed product titles')
    entries = []
    for row in titles['entries']:
        # The old model review is usable only after a selected translation exists.
        target = row.get('selected') if titles['kind'] == 'shop_detail_title_model_review' else row.get('translation')
        if not isinstance(row.get('source'), str) or not isinstance(target, str) or not target:
            raise ValueError('Every product title requires an explicit reviewed translation')
        entries.append({'source': row['source'], 'translation': target})
    config = json.loads(args.config.read_text(encoding='utf-8'))
    for key in ('approved_stems', 'confirmed_single_stems'):
        value = config.get(key, [])
        if not isinstance(value, list) or any(not isinstance(stem, str) or not stem for stem in value):
            raise ValueError(f'{key} must be a list of explicit nonempty product stems')
    templates, pending = derive_templates(entries, config.get('approved_stems', []),
                                         confirmed_single_stems=config.get('confirmed_single_stems', []))
    report = {'schema_version': 1, 'scope': 'product_title_template_candidates_no_dictionary_changes',
              'inputs': [{'source_file': path.name,
                          'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                         for path in (args.titles, args.config)],
              'summary': {'reviewed_title_pairs': len(entries), 'candidate_templates': len(templates),
                          'pending_single_sample_stems': len(pending)},
              'templates': templates, 'pending_stems': pending,
              'approved_stems_without_samples': sorted(set(config.get('approved_stems', [])) -
                  {match.group(1) for row in entries if (match := NUMBERED_TITLE.fullmatch(row['source']))}),
              'confirmation_policy': {'minimum_distinct_serials': 2,
                                      'confirmed_single_stems': config.get('confirmed_single_stems', [])}}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile('w', encoding='utf-8', dir=args.output.parent,
                                     prefix='.product-template-', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    try:
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)
    print(json.dumps(report['summary'], ensure_ascii=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
