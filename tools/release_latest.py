"""Mark a verified version release as GitHub Latest without duplicating assets."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

from release_followup import GitHub, TEXT_TAG, is_text
from story_catalog_payloads import release_catalog_assets


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verified_assets(release, paths):
    remote = {a['name']: a for a in release.get('assets', [])}
    return all(path.name in remote and remote[path.name].get('size') == path.stat().st_size
               and remote[path.name].get('digest') == 'sha256:' + sha256(path) for path in paths)


def mark_latest(client, release):
    """Only change the Latest designation; version tags, notes and assets stay put."""
    if release.get('draft') or release.get('prerelease'):
        raise ValueError('Only a published stable release can be Latest')
    current = client.request('GET', 'releases/latest', missing_ok=True)
    if not current or current.get('id') != release['id']:
        client.request('PATCH', f'releases/{release["id"]}', {'make_latest': 'true'})
        current = client.request('GET', 'releases/latest')
    if current.get('id') != release['id'] or current.get('tag_name') != release['tag_name']:
        raise ValueError('GitHub Latest did not select the verified version; retry publication')


def publish_latest(repository, output, text_tag, dry_run=False, *, client=None):
    output = Path(output)
    archive, manifest_path = output / 'text-update.zip', output / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    expected_url = f'https://github.com/{repository}/releases/download/{text_tag}/text-update.zip'
    if (not TEXT_TAG.fullmatch(text_tag) or manifest.get('version') != text_tag[5:]
            or manifest.get('schema_version') != 2
            or manifest.get('archive', {}).get('url') != expected_url
            or manifest['archive'].get('sha256') != sha256(archive)):
        raise ValueError('Latest manifest must reference the verified versioned text archive')
    catalogs = release_catalog_assets(output, repository, text_tag)
    if dry_run:
        return {'tag': text_tag, 'published': False}
    client = client or GitHub(repository)
    source = client.request('GET', f'releases/tags/{text_tag}')
    if (not is_text(source) or source.get('tag_name') != text_tag
            or not verified_assets(source, [archive, manifest_path, *catalogs])):
        raise ValueError('Text release is not complete and verified; Latest unchanged')
    mark_latest(client, source)
    return {'tag': text_tag, 'published': True}
