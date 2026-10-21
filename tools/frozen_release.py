"""Frozen collaboration projection and single-parent publication transactions.

Only allowlisted data enters main. Collaboration is read at one immutable SHA;
this module never updates its branch or executes any file from that checkout.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from export_translation_sources import export_data_path, migration_data_path, repository_git
from import_collaboration import read_csv, safe_files

RECEIPT = 'automation/collaboration-release.json'
SHA = re.compile(r'[0-9a-f]{40,64}\Z')


def hashed(data):
    return hashlib.sha256(data).hexdigest() if data is not None else None


def read_data(root, relative):
    target = root / relative
    if Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('Unsafe publication data path')
    if any(p.is_symlink() for p in (target, *target.parents) if p == root or root in p.parents):
        raise ValueError('Symlinked publication data')
    return target.read_bytes() if target.is_file() else None


def eligible_path(path):
    return export_data_path(path) or (migration_data_path(path) and path.startswith(('story/human/', 'story/reviewed/', 'records/')))


def data_tree(root):
    return {p.relative_to(root).as_posix(): read_data(root, p.relative_to(root).as_posix())
            for p in root.rglob('*') if '.git' not in p.relative_to(root).parts and p.is_file()
            and eligible_path(p.relative_to(root).as_posix())}


def working_data(root):
    """Historical main work data is explicitly retired during initial rollout."""
    return {p.relative_to(root).as_posix(): read_data(root, p.relative_to(root).as_posix())
            for p in root.rglob('*') if p.is_file() and
            p.relative_to(root).as_posix().startswith(('story/drafts/', 'archive/backups/', 'proofread_txt/'))
            and migration_data_path(p.relative_to(root).as_posix())}


def verify_work_preserved(main, collaboration):
    working = working_data(main)
    if not working: return
    # The current draft may have changed since branching. Its older blob must
    # still be reachable in permanent collaboration history before main retires it.
    reachable = set()
    paths = sorted(working)
    for start in range(0, len(paths), 100):
        reachable.update(line.split(' ', 1)[0] for line in repository_git(collaboration,
            'rev-list', '--objects', 'HEAD', '--', *paths[start:start + 100]).splitlines())
    for path in working:
        if repository_git(main, 'hash-object', str(main / path)) not in reachable:
            raise ValueError('Preserve historical main work on collaboration before rollout: ' + path)


def projection(root, *, previous=None, published=None):
    """Exclude unfinished work; retain a published formal artifact during editing.

Removing the formal artifact is an intentional withdrawal. A draft or an
in-progress task alone cannot replace a previously published completed text.
    """
    previous, published = previous or {}, published or {}
    data = {p: value for p, value in data_tree(root).items() if export_data_path(p)}
    records, ignored = {}, set()
    for layer, track in (('human', 'translation'), ('reviewed', 'proofread')):
        for path in safe_files(root / 'story' / layer, '*.csv'):
            relative = path.relative_to(root).as_posix()
            stem, info, rows = read_csv(path)
            record_path = 'records/' + stem + '.json'
            raw = read_data(root, record_path)
            record = json.loads(raw) if raw else {}
            state = record.get(track, {})
            artifact = record.get('artifacts', {}).get(track + '_csv', {})
            confirmed = record.get('source_change', {}).get('status') != 'needs-confirmation'
            if record.get('source_change', {}).get('status') == 'confirmed':
                confirmation = record.get('source_confirmation', {})
                if (record['source_change'].get('source_sha256') != info['text'] or
                        confirmation.get('source_sha256') != info['text'] or
                        not isinstance(confirmation.get('confirmed_by'), str) or
                        not confirmation['confirmed_by'].strip()):
                    raise ValueError(f'Invalid source confirmation: {stem}')
            complete = state.get('state') == '完成' and confirmed
            if not complete:
                if confirmed and relative in previous and relative in published:
                    data[relative] = published[relative]
                    ignored.add(relative)
                    old = json.loads(published.get(record_path, b'{}'))
                    if track in old:
                        entry = records.setdefault(record_path, {'schema_version': 1, 'file_id': stem, 'artifacts': {}})
                        entry[track] = old[track]
                        entry['artifacts'][track + '_csv'] = old['artifacts'][track + '_csv']
                continue
            if (record.get('schema_version') != 1 or record.get('file_id') != stem or
                    type(state.get('revision')) is not int or state['revision'] < 1 or artifact.get('path') != relative):
                raise ValueError(f'Invalid completed record: {stem}/{track}')
            if any(r['text'] and not r['trans'].strip() for r in rows if r['id'] not in ('info', '译者')):
                raise ValueError(f'Completed artifact has empty translation: {stem}/{track}')
            data[relative] = path.read_bytes()
            entry = records.setdefault(record_path, {'schema_version': 1, 'file_id': stem, 'artifacts': {}})
            entry[track] = {'state': '完成', 'revision': state['revision']}
            entry['artifacts'][track + '_csv'] = {'path': relative}
    for path, record in records.items():
        data[path] = (json.dumps(record, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode()
    hashes = {p: hashed(v) for p, v in data.items()}
    for path in ignored:
        hashes[path] = previous[path]
    return data, hashes


def load_receipt(repository):
    raw = read_data(repository, RECEIPT)
    if raw is None: return None
    receipt = json.loads(raw)
    if (receipt.get('schema_version') != 1 or not SHA.fullmatch(receipt.get('cutoff', '')) or
            not SHA.fullmatch(receipt.get('main_parent', '')) or
            not re.fullmatch(r'[0-9a-f]{16}', receipt.get('text_version', ''))):
        raise ValueError('Invalid frozen publication receipt')
    for key in ('collaboration_baseline', 'published_data'):
        if not isinstance(receipt.get(key), dict) or any(not eligible_path(p) or not re.fullmatch(r'[0-9a-f]{64}', h)
                for p, h in receipt[key].items()):
            raise ValueError('Invalid frozen publication baseline')
    return receipt


def write_tree(root, data):
    for relative, value in data.items():
        read_data(root, relative)  # Validate symlink boundaries before writing.
        target = root / relative
        if value is None:
            target.unlink(missing_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(value)


def upstream_at(repository, commit, expected_hash):
    """Read the immutable provenance recorded by a publication receipt."""
    if not SHA.fullmatch(commit):
        raise ValueError('Invalid upstream baseline commit')
    raw = subprocess.run(['git', '-C', str(repository), 'cat-file', 'blob',
                          commit + ':upstream.json'], check=True, capture_output=True).stdout
    if hashed(raw) != expected_hash:
        raise ValueError('Upstream baseline does not match publication receipt')
    return raw


def merge_upstream_sections(before, after, previous, baseline):
    """Apply collaboration deltas without reverting independently refreshed sources.

    Story and MasterDB snapshots are separate provenance groups. Keep each group
    atomic so a version, counts and digest cannot come from different snapshots.
    The collaboration and published baselines may themselves differ after refresh.
    """
    current, incoming, old_collaboration, old_published = [
        json.loads(raw) for raw in (before, after, previous, baseline)]
    if any(not isinstance(value, dict) for value in
           (current, incoming, old_collaboration, old_published)):
        raise ValueError('Upstream provenance must be a JSON object')
    missing = object()
    merged = dict(current)
    for key in sorted(set(old_collaboration) | set(incoming)):
        new_value = incoming.get(key, missing)
        if old_collaboration.get(key, missing) == new_value:
            continue
        current_value = current.get(key, missing)
        if current_value != old_published.get(key, missing) and current_value != new_value:
            raise ValueError('Main and collaboration both changed publication data: upstream.json/' + key)
        if new_value is missing:
            merged.pop(key, None)
        else:
            merged[key] = new_value
    if merged == current:
        return before
    return (json.dumps(merged, ensure_ascii=False, indent=2) + '\n').encode()


def stage_cutoff(main, collaboration, destination):
    """Apply only collaboration changes since the previous published projection."""
    for checkout in (main, collaboration):
        if repository_git(checkout, 'status', '--porcelain', '--untracked-files=all'):
            raise ValueError('Frozen release requires clean input checkouts')
    verify_work_preserved(main, collaboration)
    receipt = load_receipt(main)
    main_data = data_tree(main)
    if receipt:
        repository_git(collaboration, 'merge-base', '--is-ancestor', receipt['cutoff'], 'HEAD')
        previous, baseline = receipt['collaboration_baseline'], receipt['published_data']
        previous_repository, previous_commit = collaboration, receipt['cutoff']
        published_commit = receipt_commit(main)
    else:
        # Bootstrap against the actual common ancestor, never assume equal trees.
        git = lambda *args: repository_git(main, *args)
        cutoff = repository_git(collaboration, 'rev-parse', 'HEAD')
        git('fetch', str(collaboration), cutoff)
        ancestor = git('merge-base', 'HEAD', cutoff)
        previous_repository, previous_commit = main, ancestor
        published_commit = ancestor
        with tempfile.TemporaryDirectory(prefix='idoly-publication-base-') as tmp:
            checkout = Path(tmp) / 'checkout'
            git('worktree', 'add', '--detach', str(checkout), ancestor)
            try:
                _, previous = projection(checkout)
                baseline = {p: hashed(v) for p, v in data_tree(checkout).items()}
            finally:
                git('worktree', 'remove', '--force', str(checkout))
    incoming, hashes = projection(collaboration, previous=previous, published=main_data)
    changes = {}
    for path in sorted(set(previous) | set(hashes)):
        if previous.get(path) == hashes.get(path): continue
        before, after = main_data.get(path), incoming.get(path)
        if (path == 'upstream.json' and before is not None and after is not None
                and previous.get(path) is not None and baseline.get(path) is not None):
            changes[path] = merge_upstream_sections(before, after,
                upstream_at(previous_repository, previous_commit, previous[path]),
                upstream_at(main, published_commit, baseline[path]))
            continue
        if hashed(before) != baseline.get(path) and before != after:
            raise ValueError('Main and collaboration both changed publication data: ' + path)
        changes[path] = after
    if destination.exists(): shutil.rmtree(destination)
    shutil.copytree(main, destination, ignore=shutil.ignore_patterns('.git'))
    write_tree(destination, changes)
    write_tree(destination, {p: None for p in working_data(destination)})
    # Main may contain historical draft/record fields from the old architecture.
    # Sanitize the full result, including records which did not change today.
    normalized, _ = projection(destination)
    all_data = data_tree(destination)
    write_tree(destination, {p: normalized.get(p) for p in set(all_data) | set(normalized)})
    return hashes


def unchanged_batch(main, stage, content_version):
    receipt = load_receipt(main)
    return bool(receipt and receipt.get('content_version') == content_version and
                data_tree(main) == data_tree(stage) and not working_data(main))


def commit_batch(main, stage, expected_head, receipt, branch='main'):
    """One commit, one parent, normal fast-forward push; races fail closed."""
    git = lambda *args: repository_git(main, *args)
    if git('rev-parse', 'HEAD') != expected_head or git('status', '--porcelain', '--untracked-files=all'):
        raise ValueError('Trusted main checkout changed during release')
    git('fetch', 'origin', branch)
    if git('rev-parse', 'FETCH_HEAD') != expected_head:
        raise ValueError('Main advanced after batch freeze; retry the same frozen batch after reconciliation')
    before, after = data_tree(main), data_tree(stage)
    changes = {p: after.get(p) for p in set(before) | set(after) if before.get(p) != after.get(p)}
    changes.update({p: None for p in working_data(main)})
    receipt['published_data'] = {p: hashed(v) for p, v in after.items()}
    changes[RECEIPT] = (json.dumps(receipt, ensure_ascii=False, sort_keys=True, indent=2) + '\n').encode()
    with tempfile.TemporaryDirectory(prefix='idoly-main-batch-') as tmp:
        checkout = Path(tmp) / 'checkout'
        git('worktree', 'add', '--detach', str(checkout), expected_head)
        try:
            write_tree(checkout, changes)
            local = lambda *args: repository_git(checkout, *args)
            paths = sorted(changes)
            for start in range(0, len(paths), 100): local('add', '--', *paths[start:start + 100])
            staged = local('diff', '--cached', '--name-only').splitlines()
            if set(staged) - set(paths): raise ValueError('Unexpected staged publication files')
            local('-c', 'user.name=Idoly localization bot', '-c', 'user.email=41898282+github-actions[bot]@users.noreply.github.com',
                  'commit', '-m', 'Publish frozen collaboration batch ' + receipt['cutoff'][:12])
            commit = local('rev-parse', 'HEAD')
            try:
                local('push', 'origin', 'HEAD:refs/heads/' + branch)
            except subprocess.CalledProcessError:
                git('fetch', 'origin', branch)
                # Lost push response may still have committed successfully.
                if git('rev-parse', 'FETCH_HEAD') != commit: raise
            return commit, sorted(changes)
        finally:
            git('worktree', 'remove', '--force', str(checkout))


def receipt_commit(repository):
    return repository_git(repository, 'log', '-1', '--format=%H', '--', RECEIPT)


def cache_assets(work, version, output, apk=None, notes=None):
    from story_catalog_payloads import release_catalog_assets
    target = work / 'frozen-batches' / version
    target.mkdir(parents=True, exist_ok=True)
    assets = ([output / 'manifest.json', output / 'text-update.zip']
              + release_catalog_assets(output) + ([apk] if apk else []))
    hashes = {}
    for asset in assets:
        destination = target / asset.name
        if destination.exists() and destination.read_bytes() != asset.read_bytes():
            raise ValueError('Frozen asset cache differs; refusing replacement')
        shutil.copyfile(asset, destination)
        hashes[asset.name] = hashed(asset.read_bytes())
    if notes is not None: (target / 'plugin-notes.txt').write_text(notes)
    return hashes


def cached_assets(work, receipt):
    target = work / 'frozen-batches' / receipt['text_version']
    for name, sha in receipt['assets'].items():
        if Path(name).name != name or hashed(read_data(target, name)) != sha:
            raise ValueError('Frozen assets missing or changed; restore runner frozen-batches cache before retrying publication')
    return target
