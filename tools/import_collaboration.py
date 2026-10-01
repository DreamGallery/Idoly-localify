#!/usr/bin/env python3
"""Import validated Viewer data into an isolated release workspace; never execute it."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import tempfile

COLUMNS = ['id', 'name', 'text', 'trans']
STEM = re.compile(r'adv_[A-Za-z0-9_-]+\Z')

def read_csv(path):
    if path.is_symlink() or path.stat().st_size > 8 * 1024 * 1024:
        raise ValueError(f'Unsafe CSV: {path}')
    with path.open(encoding='utf-8-sig', newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != COLUMNS:
            raise ValueError(f'Invalid CSV columns: {path}')
        rows = list(reader)
    if any(set(r) != set(COLUMNS) or any(v is None for v in r.values()) for r in rows):
        raise ValueError(f'Malformed CSV: {path}')
    infos = [r for r in rows if r['id'] == 'info']
    if len(infos) != 1 or not infos[0]['name'].endswith('.txt'):
        raise ValueError(f'Missing source metadata: {path}')
    stem = infos[0]['name'][:-4]
    if not STEM.fullmatch(stem):
        raise ValueError(f'Unsafe source ID: {path}')
    return stem, infos[0], rows

def safe_files(root, pattern):
    for path in sorted(root.rglob(pattern)):
        if any(p.is_symlink() for p in [path, *path.parents] if p == root or root in p.parents):
            raise ValueError(f'Symlinked data: {path}')
        yield path

def copy_data(repository, project):
    # Only allowlisted data trees enter the private code workspace.
    for directory in ('master', 'notice', 'legal', 'ui', 'glossary', 'story-metadata'):
        source = repository / directory
        if not source.is_dir():
            raise ValueError(f'Missing translation directory: {directory}')
        for path in safe_files(source, '*.json'):
            if path.stat().st_size > 128 * 1024 * 1024:
                raise ValueError(f'Oversized data: {path}')
            json.loads(path.read_text(encoding='utf-8'))
            target = project / 'translations' / directory / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)

def import_stories(repository, source_csv, source_txt, target_csv, project, human_only=False, apply_human_titles=True, rebase_ai=False, ai_only=False):
    originals = {}
    for path in safe_files(source_csv, '*.csv'):
        stem, info, rows = read_csv(path)
        if stem in originals:
            raise ValueError(f'Duplicate original story: {stem}')
        txt = source_txt / (stem + '.txt')
        if txt.is_symlink() or hashlib.sha256(txt.read_bytes()).hexdigest() != info['text']:
            raise ValueError(f'Stale original CSV: {stem}')
        originals[stem] = (info, rows)
    selected = {}
    rebased = {}
    for layer in (('ai',) if ai_only else ('human', 'reviewed') if human_only else ('ai', 'human', 'reviewed')):
        seen = set()
        for path in safe_files(repository / 'story' / layer, '*.csv'):
            stem, info, rows = read_csv(path)
            if stem in seen:
                raise ValueError(f'Duplicate {layer} story: {stem}')
            seen.add(stem)
            if stem not in originals:
                raise ValueError(f'Orphan translated story: {stem}')
            source_info, source_rows = originals[stem]
            source_fields = [(r['id'], r['name'], r['text']) for r in source_rows if r['id'] != '译者']
            fields = [(r['id'], r['name'], r['text']) for r in rows if r['id'] != '译者']
            if info != source_info or fields != source_fields:
                if layer != 'ai' or not rebase_ai:
                    raise ValueError(f'Stale source or changed original fields: {path}')
                from workspace_paths import load_module
                toolkit = load_module('HoshimiToolkit', 'src.adv_csv')
                with tempfile.TemporaryDirectory(prefix='idoly-ai-rebase-') as temporary:
                    candidate = Path(temporary) / (stem + '.csv')
                    preserved, lost = toolkit.rebase_file(source_txt / (stem + '.txt'), path, candidate)
                    _, info, rows = read_csv(candidate)
                fields = [(r['id'], r['name'], r['text']) for r in rows if r['id'] != '译者']
                if info != source_info or fields != source_fields:
                    raise ValueError(f'Rebased AI source still mismatches: {stem}')
                rebased[stem] = {'preserved_fields': preserved, 'retired_fields': len(lost)}
            if layer != 'ai':
                record_path = repository / 'records' / (stem + '.json')
                if record_path.is_symlink():
                    raise ValueError('Symlinked completion record')
                record = json.loads(record_path.read_text(encoding='utf-8'))
                track, artifact = ('proofread', 'proofread_csv') if layer == 'reviewed' else ('translation', 'translation_csv')
                if (record.get('schema_version') != 1 or record.get('file_id') != stem or
                    record.get(track, {}).get('state') != '完成' or
                    type(record.get(track, {}).get('revision')) is not int or record[track]['revision'] < 1 or
                    record.get('artifacts', {}).get(artifact, {}).get('path') != path.relative_to(repository).as_posix()):
                    raise ValueError(f'Unfinished or mismatched Viewer record: {stem}/{layer}')
                if any(row['text'] and not row['trans'].strip() for row in rows if row['id'] not in ('info', '译者')):
                    raise ValueError(f'Completed human artifact has empty translations: {stem}/{layer}')
            selected[stem] = (layer, path, rows)
    target_csv.mkdir(parents=True, exist_ok=True)
    # Source-only chapters remain empty for the incremental translator.
    for stem, (_, rows) in originals.items():
        final = selected.get(stem, ('source', None, rows))[2]
        with (target_csv / (stem + '.csv')).open('w', encoding='utf-8', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=COLUMNS, lineterminator='\n')
            writer.writeheader(); writer.writerows(final)
    overrides = apply_titles(project, selected) if apply_human_titles else []
    return {'schema_version': 1, 'stories': {stem: {'layer': layer,
            'csv_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'source_sha256': originals[stem][0]['text']} for stem, (layer, path, _) in selected.items()},
            'master_title_overrides': overrides, 'rebased_ai': rebased}

def validate_story_coverage(csv_dir):
    missing = []
    for path in safe_files(csv_dir, '*.csv'):
        stem, _, rows = read_csv(path)
        for row in rows:
            if row['id'] not in ('info', '译者') and row['text'] and not row['trans'].strip():
                missing.append(f"{stem}:{row['id']}")
    if missing:
        raise ValueError(f'Final story coverage incomplete ({len(missing)} fields): {missing[:5]}')


def apply_titles(project, selected):
    master = project / 'translations/master'
    catalog = json.loads((project / 'translations/story-metadata/catalog.json').read_text())['stories']
    original = json.loads((master / 'orig/Story.json').read_text())
    translated = json.loads((master / 'zh-Hans/Story.json').read_text())
    changes = []
    for record_id, story in catalog.items():
        candidates = set()
        for asset in story.get('assets', []):
            stem = asset if asset.startswith('adv_') else 'adv_' + asset
            if stem not in selected or selected[stem][0] != 'reviewed':
                continue
            for row in selected[stem][2]:
                if ':title:' not in row['id'] or not row['trans']:
                    continue
                pairs = [(row['text'], row['trans'])]
                source = re.search(r'\d+話\s+(.+)$', row['text'])
                target = re.search(r'\d+话\s+(.+)$', row['trans'])
                if source and target: pairs.append((source[1], target[1]))
                for text, value in pairs:
                    if original.get(record_id, {}).get('name') == text:
                        candidates.add(value)
        if len(candidates) > 1:
            raise ValueError(f'Conflicting human titles for Story:{record_id}')
        if candidates:
            value = next(iter(candidates))
            translated.setdefault(record_id, {})['name'] = value
            changes.append({'table': 'Story', 'id': record_id, 'field': 'name',
                            'source': original[record_id]['name'], 'translation': value})
    (master / 'zh-Hans/Story.json').write_text(json.dumps(translated, ensure_ascii=False, indent=2) + '\n')
    return changes

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('repository', 'project', 'source-csv', 'source-txt', 'target-csv', 'receipt'):
        parser.add_argument('--' + arg, type=Path, required=True)
    parser.add_argument("--human-only", action="store_true")
    parser.add_argument("--rebase-ai", action="store_true")
    args = parser.parse_args()
    if not args.human_only:
        copy_data(args.repository, args.project)
    result = import_stories(args.repository, args.source_csv, args.source_txt, args.target_csv, args.project, args.human_only, rebase_ai=args.rebase_ai)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps({'stories': len(result['stories']), 'human_titles': len(result['master_title_overrides'])}))
if __name__ == '__main__': main()
