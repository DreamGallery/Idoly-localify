"""Maintain the mutable text download entry after an immutable version is ready."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess

from release_followup import GitHub, PLUGIN_TAG, TEXT_ASSETS, TEXT_TAG, find_release, is_text, timestamp


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verified_assets(release, paths):
    remote = {a['name']: a for a in release.get('assets', [])}
    return all(path.name in remote and remote[path.name].get('size') == path.stat().st_size
               and remote[path.name].get('digest') == 'sha256:' + sha256(path) for path in paths)


def latest_plugin(releases):
    candidates = []
    for release in releases:
        tag = release.get('tag_name', '')
        if not PLUGIN_TAG.fullmatch(tag) or release.get('draft') or release.get('prerelease'):
            continue
        expected = f'idoly-localify-{tag[1:]}.apk'
        assets = [a for a in release.get('assets', []) if a['name'] == expected]
        if (len(assets) == 1 and assets[0].get('size', 0) > 0
                and re.fullmatch(r'sha256:[0-9a-f]{64}', assets[0].get('digest') or '')):
            candidates.append(release)
    return max(candidates, key=timestamp, default=None)


def notes(repository, text_tag, plugin):
    base = f'https://github.com/{repository}/releases'
    lines = ['## 最新插件', '']
    if plugin:
        tag = plugin['tag_name']
        lines += [f'**[{tag} · 下载插件 APK]({base}/download/{tag}/idoly-localify-{tag[1:]}.apk)**'
                  f' · [更新说明与安装方法]({base}/tag/{tag})', '']
    else:
        lines += ['尚无正式插件版本。', '']
    lines += ['## 最新文本', '', f'当前版本：[`{text_tag}`]({base}/tag/{text_tag})', '',
              f'插件文本更新地址：`{base}/download/latest/manifest.json`', '',
              '本页附件会随文本更新；更新后请重启游戏以载入 MasterDB。']
    return '\n'.join(lines) + '\n'


def upload(repository, path):
    subprocess.run(['gh', 'release', 'upload', 'latest', str(path), '--repo', repository,
                    '--clobber'], check=True)


def publish_latest(repository, output, text_tag, dry_run=False, *, client=None, uploader=None):
    """Keep historical URLs in manifest so replacing alias assets cannot mix versions."""
    output = Path(output)
    archive, manifest_path = output / 'text-update.zip', output / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    expected_url = f'https://github.com/{repository}/releases/download/{text_tag}/text-update.zip'
    if (not TEXT_TAG.fullmatch(text_tag) or manifest.get('version') != text_tag[5:]
            or manifest.get('schema_version') != 2
            or manifest.get('archive', {}).get('url') != expected_url
            or manifest['archive'].get('sha256') != sha256(archive)):
        raise ValueError('Latest manifest must reference the verified historical text archive')
    if dry_run:
        return {'tag': 'latest', 'text_tag': text_tag, 'published': False}
    client = client or GitHub(repository)
    uploader = uploader or upload
    source = client.request('GET', f'releases/tags/{text_tag}')
    if not is_text(source) or not verified_assets(source, [archive, manifest_path]):
        raise ValueError('Historical text release is not complete and verified; latest unchanged')
    alias = find_release(client, 'latest')
    if alias:
        if alias.get('immutable'):
            raise ValueError('The latest release is immutable; disable release immutability before using a moving alias')
        if alias.get('prerelease') or {a['name'] for a in alias.get('assets', [])} - TEXT_ASSETS:
            raise ValueError('The latest release contains unrelated assets or is a prerelease')
    commit = client.request('GET', f'commits/{text_tag}')['sha']
    if not re.fullmatch(r'[0-9a-f]{40,64}', commit):
        raise ValueError('Historical text tag does not identify a valid commit')
    body = notes(repository, text_tag, latest_plugin(client.releases()))
    metadata = {'name': '最新文本与插件', 'body': body, 'make_latest': 'true',
                'draft': False, 'prerelease': False}
    if not alias:
        alias = client.request('POST', 'releases',
                               {**metadata, 'tag_name': 'latest', 'target_commitish': commit,
                                'draft': True, 'make_latest': 'false'})
    # GitHub asset replacement is not atomic. Publish the ZIP first and manifest
    # last; clients follow the versioned ZIP URL, never the mutable ZIP alias.
    for path in (archive, manifest_path):
        if not verified_assets(alias, [path]):
            uploader(repository, path)
            alias = client.request('GET', f'releases/{alias["id"]}')
            if not verified_assets(alias, [path]):
                raise ValueError(f'Latest asset verification failed: {path.name}')
    if not verified_assets(alias, [archive, manifest_path]):
        raise ValueError('Latest assets changed during publication')
    # Only this explicitly mutable tag may be moved, after asset verification.
    ref = client.request('GET', 'git/ref/tags/latest', missing_ok=True)
    if ref is None:
        client.request('POST', 'git/refs', {'ref': 'refs/tags/latest', 'sha': commit})
    elif ref['object']['sha'] != commit:
        client.request('PATCH', 'git/refs/tags/latest', {'sha': commit, 'force': True})
    if any(alias.get(key) != value for key, value in metadata.items() if key != 'make_latest'):
        client.request('PATCH', f'releases/{alias["id"]}', metadata)
    elif client.request('GET', 'releases/latest')['tag_name'] != 'latest':
        client.request('PATCH', f'releases/{alias["id"]}', {'make_latest': 'true'})
    return {'tag': 'latest', 'text_tag': text_tag, 'published': True}
