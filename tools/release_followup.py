"""Explicit post-release dispatch and narrowly scoped text-release retention."""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import quote

TEXT_TAG = re.compile(r'text-[0-9a-f]{16}')
PLUGIN_TAG = re.compile(r'v\d+\.\d+\.\d+(?:[-.][A-Za-z0-9]+)*')
TEXT_ASSETS = {'manifest.json', 'text-update.zip'}


class GitHub:
    def __init__(self, repository, token=None):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
            raise ValueError('Invalid repository')
        self.repository, self.token = repository, token

    def request(self, method, path, data=None, missing_ok=False):
        command = ['gh', 'api', '--method', method, f'repos/{self.repository}/{path}']
        if data is not None:
            command += ['--input', '-']
        env = os.environ.copy()
        if self.token:
            env['GH_TOKEN'] = self.token
        result = subprocess.run(command, input=json.dumps(data) if data is not None else None,
                                capture_output=True, text=True, env=env)
        if result.returncode:
            if missing_ok and '(HTTP 404)' in result.stderr:
                return None
            raise RuntimeError(f'GitHub {method} failed; check permissions/connectivity (no credentials logged)')
        return json.loads(result.stdout) if result.stdout.strip() else None

    def releases(self):
        result = []
        for page in range(1, 101):
            items = self.request('GET', f'releases?per_page=100&page={page}')
            result.extend(items)
            if len(items) < 100:
                return result
        raise ValueError('Release listing exceeded pagination limit')

    def release_manifest(self, release):
        assets = [a for a in release.get('assets', []) if a['name'] == 'manifest.json']
        if len(assets) != 1 or not 0 < assets[0].get('size', 0) <= 16 * 1024 * 1024:
            raise ValueError('Latest manifest is missing or invalid; cleanup stopped')
        env = os.environ.copy()
        if self.token:
            env['GH_TOKEN'] = self.token
        with tempfile.TemporaryDirectory(prefix='idoly-retention-') as temporary:
            result = subprocess.run(['gh', 'release', 'download', release['tag_name'],
                                     '--repo', self.repository, '--pattern', 'manifest.json',
                                     '--dir', temporary], env=env, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError('Cannot read latest manifest; cleanup stopped')
            data = (Path(temporary) / 'manifest.json').read_bytes()
        if (len(data) != assets[0]['size']
                or assets[0].get('digest') != 'sha256:' + hashlib.sha256(data).hexdigest()):
            raise ValueError('Latest manifest changed while reading; cleanup stopped')
        return json.loads(data)


def find_release(client, tag):
    release = client.request('GET', 'releases/tags/' + quote(tag, safe=''), missing_ok=True)
    if release is not None:
        return release
    # Drafts can be listed by an authorized publisher before a tag exists.
    matches = [item for item in client.releases() if item.get('tag_name') == tag]
    if len(matches) > 1:
        raise ValueError('Multiple releases use the selected tag')
    return matches[0] if matches else None


def dispatch(source, target, tag):
    if not PLUGIN_TAG.fullmatch(tag):
        raise ValueError('Expected a plugin release tag')
    release = source.request('GET', 'releases/tags/' + quote(tag, safe=''))
    expected = f'idoly-localify-{tag[1:]}.apk'
    assets = [a for a in release.get('assets', []) if a['name'] == expected]
    if (release.get('tag_name') != tag or release.get('draft') or release.get('prerelease')
            or len(assets) != 1 or not assets[0].get('size', 0)
            or not re.fullmatch(r'sha256:[0-9a-f]{64}', assets[0].get('digest') or '')):
        raise ValueError('Plugin release is not published with a complete, checksummed APK')
    target.request('POST', 'actions/workflows/patch.yml/dispatches',
                   {'ref': 'main', 'inputs': {'module_tag': tag, 'automatic': True, 'publish': True}})
    return {'dispatched_plugin': tag, 'target': target.repository}


def is_text(release):
    return (bool(TEXT_TAG.fullmatch(release.get('tag_name', '')))
            and not release.get('draft') and not release.get('prerelease')
            and {a['name'] for a in release.get('assets', [])} == TEXT_ASSETS)


def timestamp(release):
    value = datetime.fromisoformat(release['published_at'].replace('Z', '+00:00'))
    if value.tzinfo is None:
        raise ValueError('Release timestamp must have a timezone')
    return value


def alias_target(client):
    alias = client.request('GET', 'releases/tags/latest', missing_ok=True)
    if alias is None or alias.get('draft'):
        return None
    if alias.get('prerelease') or {a['name'] for a in alias.get('assets', [])} != TEXT_ASSETS:
        raise ValueError('Unexpected latest release; cleanup stopped')
    manifest = client.release_manifest(alias)
    tag = 'text-' + str(manifest.get('version', ''))
    expected_url = f'https://github.com/{client.repository}/releases/download/{tag}/text-update.zip'
    if (not TEXT_TAG.fullmatch(tag) or manifest.get('schema_version') != 2
            or manifest.get('archive', {}).get('url') != expected_url):
        raise ValueError('Unexpected latest manifest target; cleanup stopped')
    return tag


def cleanup_plan(releases, keep, current, latest, referenced=None):
    if keep < 1 or keep > 1000:
        raise ValueError('Retention must be 1–1000, or 0 to disable cleanup')
    texts = sorted((r for r in releases if is_text(r)), key=timestamp, reverse=True)
    if current not in {r['tag_name'] for r in texts}:
        raise ValueError('Current text release is missing or incomplete; cleanup stopped')
    if referenced and referenced not in {r['tag_name'] for r in texts}:
        raise ValueError('Latest references a missing text release; cleanup stopped')
    protected = {current, latest, 'latest'} | {r['tag_name'] for r in texts[:keep]}
    if referenced:
        protected.add(referenced)
    return [r for r in texts if r['tag_name'] not in protected], protected


def save_journal(path, journal):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(journal, indent=2) + '\n', encoding='utf-8')
    temporary.chmod(0o600)
    temporary.replace(path)


def cleanup(client, current, keep, journal_path, apply=False):
    if keep == 0:
        return {'cleanup': 'disabled'}
    if not TEXT_TAG.fullmatch(current):
        raise ValueError('Expected the successfully published text tag')
    latest = client.request('GET', 'releases/latest')['tag_name']
    referenced = alias_target(client)
    candidates, protected = cleanup_plan(client.releases(), keep, current, latest, referenced)
    journal = {'schema_version': 1, 'repository': client.repository, 'pending': {}}
    if journal_path.is_file():
        journal = json.loads(journal_path.read_text(encoding='utf-8'))
        if journal.get('schema_version') != 1 or journal.get('repository') != client.repository:
            raise ValueError('Cleanup journal belongs to another repository')
    pending = journal['pending']
    for tag, item in pending.items():
        if (not TEXT_TAG.fullmatch(tag) or not isinstance(item.get('release_id'), int)
                or not re.fullmatch(r'[0-9a-f]{40,64}', item.get('ref_sha', ''))):
            raise ValueError('Invalid cleanup recovery journal')
    selected = {r['tag_name']: r for r in candidates}
    tags = sorted((set(selected) | set(pending)) - protected)
    if not apply:
        return {'would_delete': tags, 'protected': sorted(protected), 'keep': keep}
    removed = []
    for tag in tags:
        # Recheck mutable remote state immediately before each destructive step.
        if tag in {client.request('GET', 'releases/latest')['tag_name'], alias_target(client)}:
            continue
        path = 'releases/tags/' + quote(tag, safe='')
        release = client.request('GET', path, missing_ok=True)
        if release is not None:
            expected_id = pending[tag]['release_id'] if tag in pending else selected[tag]['id']
            if release['id'] != expected_id or not is_text(release):
                raise ValueError('Release changed since cleanup planning; stopped')
        elif tag not in pending:
            # Do not infer permission to delete orphan tags we did not remove.
            continue
        ref_path = 'git/ref/tags/' + quote(tag, safe='')
        ref = client.request('GET', ref_path, missing_ok=True)
        if tag in pending:
            if ref is not None and ref['object']['sha'] != pending[tag]['ref_sha']:
                raise ValueError('Tag moved since cleanup began; stopped')
        elif ref is not None:
            pending[tag] = {'release_id': release['id'], 'ref_sha': ref['object']['sha']}
            save_journal(journal_path, journal)
        if release is not None:
            client.request('DELETE', f'releases/{release["id"]}')
        if ref is not None:
            if (client.request('GET', path, missing_ok=True) is not None
                    or tag in {client.request('GET', 'releases/latest')['tag_name'], alias_target(client)}):
                raise ValueError('Release was recreated during cleanup; tag retained')
            fresh_ref = client.request('GET', ref_path, missing_ok=True)
            if fresh_ref is not None:
                if fresh_ref['object']['sha'] != pending[tag]['ref_sha']:
                    raise ValueError('Tag moved during cleanup; tag retained')
                client.request('DELETE', 'git/refs/tags/' + quote(tag, safe=''))
        pending.pop(tag, None)
        save_journal(journal_path, journal)
        removed.append(tag)
    return {'deleted': removed, 'keep': keep, 'protected': sorted(protected)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    notify = commands.add_parser('dispatch')
    notify.add_argument('--repository', required=True)
    notify.add_argument('--target', required=True)
    notify.add_argument('--tag', required=True)
    prune = commands.add_parser('cleanup')
    prune.add_argument('--repository', required=True)
    prune.add_argument('--current-tag', required=True)
    prune.add_argument('--keep', type=int, default=3)
    prune.add_argument('--config', type=Path, required=True)
    prune.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    client = GitHub(args.repository)
    if args.command == 'dispatch':
        token = os.environ.get('PATCHER_DISPATCH_TOKEN', '').strip()
        if not token:
            raise ValueError('Configure PATCHER_DISPATCH_TOKEN (target repository Actions: write)')
        result = dispatch(client, GitHub(args.target, token), args.tag)
    else:
        config = json.loads(args.config.read_text(encoding='utf-8'))
        result = cleanup(client, args.current_tag, args.keep,
                         Path(config['work_dir']) / 'text-release-cleanup.json', args.apply)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == '__main__':
    main()
