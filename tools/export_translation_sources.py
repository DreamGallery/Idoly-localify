#!/usr/bin/env python3
"""Export validated machine-source data without touching Viewer human/record files."""
from __future__ import annotations
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

from import_collaboration import read_csv, safe_files

JSON_PATHS = ('master', 'notice', 'ui', 'legal', 'story-metadata', 'glossary')
JSON_DIRS = ('master/orig', 'master/zh-Hans')
JSON_FILES = ('master/overrides.json', 'master/retained-titles.json',
              'notice/source.json', 'notice/zh-Hans.json', 'notice/title-resolutions.json',
              'ui/source.json', 'ui/zh-Hans.json', 'ui/variants-source.json',
              'legal/source.json', 'legal/zh-Hans.json',
              'story-metadata/catalog.json', 'story-metadata/overrides.json',
              'story-metadata/love-group-names.json',
              'glossary/names.json', 'glossary/terms.json', 'glossary/term-aliases.json')


def snapshot(paths):
    return hashlib.sha256(''.join(path.name + '\0' + hashlib.sha256(path.read_bytes()).hexdigest() + '\n'
                                  for path in sorted(paths, key=lambda path: path.name)).encode()).hexdigest()


def export_sources(project, csv_dir, source_dir, repository, skip_stories=(), summary=None, apply=False):
    from story_csv_paths import classify
    changes = {}
    inputs = []
    for relative in JSON_DIRS:
        source = project / 'translations' / relative
        if not source.is_dir(): continue
        source_names = {path.name for path in source.glob('*.json')}
        for old in (repository / relative).glob('*.json'):
            if old.name not in source_names:
                changes[(Path(relative) / old.name).as_posix()] = None
        inputs.extend((path, (Path(relative) / path.name).as_posix()) for path in sorted(source.glob('*.json')))
    inputs.extend((project / 'translations' / relative, relative) for relative in JSON_FILES
                  if (project / 'translations' / relative).is_file())
    for path, relative in inputs:
        if path.is_symlink(): raise ValueError(f'Symlinked export input: {path}')
        json.loads(path.read_text(encoding='utf-8'))
        changes[relative] = path.read_bytes()
    locations = {}
    for path in safe_files(repository / 'story/ai', '*.csv'):
        stem, _, _ = read_csv(path)
        if stem in locations: raise ValueError(f'Duplicate AI source: {stem}')
        locations[stem] = path.relative_to(repository).as_posix()
    published = set(locations.values())
    text_fields = 0
    for path in safe_files(csv_dir, '*.csv'):
        stem, info, rows = read_csv(path)
        content = [row for row in rows if row['id'] not in ('info', '译者') and row['text']]
        if not content:
            if stem in locations:
                changes[locations[stem]] = None
                published.discard(locations[stem])
            continue
        text_fields += len(content)
        if stem in skip_stories: continue
        source = source_dir / (stem + '.txt')
        if source.is_symlink() or hashlib.sha256(source.read_bytes()).hexdigest() != info['text']:
            raise ValueError(f'Stale story export: {stem}')
        relative = locations.get(stem, (Path('story/ai') / classify(stem + '.txt')).as_posix())
        changes[relative] = path.read_bytes()
        published.add(relative)
    upstream_path = repository / 'upstream.json'
    upstream = json.loads(upstream_path.read_text()) if upstream_path.exists() else {'schema_version': 1}
    story = upstream.setdefault('story', {})
    resources = list(source_dir.glob('adv_*.txt'))
    story.update(resource_files=len(resources), translated_csv_files=len(published), text_fields=text_fields,
                 resource_snapshot_sha256=snapshot(resources))
    master = upstream.setdefault('master', {})
    tables = list((project / 'translations/master/orig').glob('*.json'))
    master.update(text_tables=len(tables), orig_snapshot_sha256=snapshot(tables))
    if summary:
        if summary.get('octo', {}).get('after') is not None:
            story['resource_revision'] = summary['octo']['after']
        if summary.get('master_version'):
            master['master_tag_version'] = summary['master_version']
    else:
        # Do not carry an old version label onto a new source snapshot.
        previous = json.loads(upstream_path.read_text()) if upstream_path.exists() else {}
        if previous.get('story', {}).get('resource_snapshot_sha256') != story['resource_snapshot_sha256']:
            story.pop('resource_revision', None)
        if previous.get('master', {}).get('orig_snapshot_sha256') != master['orig_snapshot_sha256']:
            master.pop('master_tag_version', None)
    upstream.pop('release_sources', None)
    upstream.pop('snapshot_date', None)  # snapshot hashes carry the stable identity
    upstream['snapshot_hash_format'] = 'SHA-256 over UTF-8 lines sorted by filename: filename + NUL + file SHA-256 + LF'
    changes['upstream.json'] = (json.dumps(upstream, ensure_ascii=False, indent=2) + '\n').encode()
    changed = []
    for relative, data in sorted(changes.items()):
        target = repository / relative
        if target.is_symlink() or any(p.is_symlink() for p in target.parents if p == repository or repository in p.parents):
            raise ValueError(f'Symlinked export destination: {target}')
        if data is None and not target.exists(): continue
        if data is not None and target.exists() and target.read_bytes() == data: continue
        changed.append(relative)
        if apply and data is None:
            target.unlink()
        elif apply:
            target.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as stream:
                temporary = Path(stream.name); stream.write(data)
            temporary.replace(target)
    return changed


def migration_data_path(path):
    if not isinstance(path, str): return False
    parts = path.split('/')
    if not parts or Path(path).is_absolute() or '\\' in path or any(p in ('', '.', '..') for p in parts):
        return False
    if path.startswith('archive/backups/'):
        pieces = path.split('/', 4)
        return (len(pieces) == 5 and all(re.fullmatch(r'[a-f0-9]{64}', p) for p in pieces[2:4])
                and not pieces[4].startswith('archive/') and migration_data_path(pieces[4]))
    if path.startswith(('story/ai/', 'story/human/', 'story/reviewed/',
                        'story/drafts/translation/', 'story/drafts/proofread/')):
        return bool(re.fullmatch(r'adv_[A-Za-z0-9_-]+\.csv', parts[-1]))
    return bool(re.fullmatch(r'records/adv_[A-Za-z0-9_-]+\.json|proofread_txt/adv_[A-Za-z0-9_-]+\.txt', path))


def export_data_path(path):
    if not isinstance(path, str) or '\\' in path or any(p in ('', '.', '..') for p in path.split('/')): return False
    return (path == 'upstream.json' or path in JSON_FILES or
            bool(re.fullmatch(r'master/(?:orig|zh-Hans)/[^/]+\.json', path)) or
            (path.startswith('story/ai/') and path.endswith('.csv')))


def sync_git(repository, paths, expected_head, branch='main', migration_paths=()):
    def git(*args):
        return subprocess.run(['git', '-C', str(repository), '-c', 'core.quotepath=false', '-c', 'credential.helper=',
                               '-c', 'credential.helper=!gh auth git-credential', *args], check=True,
                              text=True, capture_output=True).stdout.strip()
    if not paths: return None
    if git('rev-parse', 'HEAD') != expected_head: raise ValueError('Translation checkout changed during release')
    git('fetch', 'origin', branch)
    if git('rev-parse', 'FETCH_HEAD') != expected_head:
        raise ValueError('Translation branch advanced; retry on latest data (no force push)')
    # Migration grants apply only to explicit validated receipt paths.
    allowed_migrations = set(migration_paths)
    if any(not migration_data_path(p) for p in allowed_migrations):
        raise ValueError('Unsafe migration data path')
    for path in paths:
        if not (export_data_path(path) or path in allowed_migrations):
            raise ValueError(f'Not an export data path: {path}')
    paths = sorted(set(paths))
    for offset in range(0, len(paths), 100):
        git('add', '--', *paths[offset:offset + 100])
    staged = git('diff', '--cached', '--no-renames', '--name-only').splitlines()
    if set(staged) != set(paths): raise ValueError('Unexpected staged files; refusing data commit')
    git('-c', 'user.name=Idoly localization bot', '-c', 'user.email=41898282+github-actions[bot]@users.noreply.github.com',
        'commit', '-m', 'Update verified localization sources')
    git('push', 'origin', f'HEAD:refs/heads/{branch}')
    return git('rev-parse', 'HEAD')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('project', 'csv-dir', 'source-dir', 'repository'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--collaboration-receipt', type=Path)
    parser.add_argument('--refresh-summary', type=Path)
    parser.add_argument('--resource-revision', type=int)
    parser.add_argument('--master-version')
    parser.add_argument('--apply', action='store_true', help='Write allowlisted data; otherwise plan only')
    args = parser.parse_args()
    receipt = json.loads(args.collaboration_receipt.read_text()) if args.collaboration_receipt else {}
    skip = [stem for stem, value in receipt.get('stories', {}).items() if value['layer'] != 'ai']
    summary = json.loads(args.refresh_summary.read_text()) if args.refresh_summary else None
    if args.resource_revision is not None or args.master_version:
        summary = summary or {}
        if args.resource_revision is not None: summary['octo'] = {'after': args.resource_revision}
        if args.master_version: summary['master_version'] = args.master_version
    changed = export_sources(args.project, args.csv_dir, args.source_dir, args.repository, skip, summary, args.apply)
    print(json.dumps({'changed_paths': changed, 'applied': args.apply}, ensure_ascii=False))
if __name__ == '__main__': main()
