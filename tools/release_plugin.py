"""Publish the plugin's mutable download entry in its source repository."""
from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from release_followup import GitHub, PLUGIN_TAG, find_release
from release_latest import verified_assets

DEFAULT_REPOSITORY = 'DreamGallery/Idoly-localify'
LATEST_APK = 'idoly-localify.apk'


@contextmanager
def plugin_auth():
    """Scope cross-repository credentials to sequential plugin publication only."""
    token = os.environ.get('PLUGIN_RELEASE_TOKEN', '').strip()
    if os.environ.get('GITHUB_ACTIONS') == 'true' and not token:
        raise ValueError('Configure PLUGIN_RELEASE_TOKEN (plugin repository Contents: write)')
    previous = os.environ.get('GH_TOKEN')
    try:
        if token:
            os.environ['GH_TOKEN'] = token
        yield
    finally:
        if previous is None:
            os.environ.pop('GH_TOKEN', None)
        else:
            os.environ['GH_TOKEN'] = previous


def download(repository, tag, name, directory):
    subprocess.run(['gh', 'release', 'download', tag, '--repo', repository,
                    '--pattern', name, '--dir', str(directory)], check=True)


def upload(repository, path):
    subprocess.run(['gh', 'release', 'upload', 'latest', str(path), '--repo', repository,
                    '--clobber'], check=True)


def publish_latest_plugin(repository, tag, dry_run=False, *, apk=None, client=None,
                          downloader=None, uploader=None):
    """Verify an immutable version before changing latest; never rebuild that APK."""
    if not PLUGIN_TAG.fullmatch(tag):
        raise ValueError('Expected an explicit plugin version tag')
    if dry_run:
        return {'tag': 'latest', 'plugin_tag': tag, 'published': False}
    client = client or GitHub(repository)
    downloader, uploader = downloader or download, uploader or upload
    source = client.request('GET', f'releases/tags/{tag}')
    name = f'idoly-localify-{tag[1:]}.apk'
    assets = (source or {}).get('assets', [])
    if (not source or source.get('draft') or source.get('prerelease')
            or source.get('tag_name') != tag or len(assets) != 1
            or assets[0].get('name') != name or not assets[0].get('size')
            or not re.fullmatch(r'sha256:[0-9a-f]{64}', assets[0].get('digest') or '')):
        raise ValueError('Plugin version must contain one complete checksummed APK')
    commit = client.request('GET', f'commits/{tag}')['sha']
    if not re.fullmatch(r'[0-9a-f]{40,64}', commit):
        raise ValueError('Plugin tag does not identify a valid source commit')
    alias = find_release(client, 'latest')
    if alias and (alias.get('immutable') or alias.get('prerelease')
                  or {a['name'] for a in alias.get('assets', [])} - {LATEST_APK}):
        raise ValueError('Unexpected or immutable latest plugin release; left unchanged')
    base = f'https://github.com/{repository}/releases'
    metadata = {'name': '最新插件', 'draft': False, 'prerelease': False,
                'make_latest': 'true',
                'body': f'当前版本：[{tag}]({base}/tag/{tag})\n\n' + (source.get('body') or '')}
    # The stable filename is convenient for downloads; versioned assets remain
    # the canonical inputs for Patcher and retain their original SHA-256.
    current = next((a for a in (alias or {}).get('assets', []) if a['name'] == LATEST_APK), None)
    same_apk = current and all(current.get(k) == assets[0].get(k) for k in ('size', 'digest'))
    if not same_apk:
        with tempfile.TemporaryDirectory(prefix='idoly-plugin-latest-') as temporary:
            directory = Path(temporary)
            if apk is None:
                downloader(repository, tag, name, directory)
                apk = directory / name
            if not verified_assets(source, [Path(apk)]):
                raise ValueError('Plugin APK differs from its published version; latest unchanged')
            stable = directory / LATEST_APK
            shutil.copyfile(apk, stable)
            if not alias:
                alias = client.request('POST', 'releases',
                                       {**metadata, 'tag_name': 'latest', 'target_commitish': commit,
                                        'draft': True, 'make_latest': 'false'})
            if not verified_assets(alias, [stable]):
                uploader(repository, stable)
                alias = client.request('GET', f'releases/{alias["id"]}')
                if not verified_assets(alias, [stable]):
                    raise ValueError('Latest plugin APK verification failed')
    ref = client.request('GET', 'git/ref/tags/latest', missing_ok=True)
    if ref is None:
        client.request('POST', 'git/refs', {'ref': 'refs/tags/latest', 'sha': commit})
    elif ref['object']['sha'] != commit:
        client.request('PATCH', 'git/refs/tags/latest', {'sha': commit, 'force': True})
    if (any(alias.get(k) != v for k, v in metadata.items() if k != 'make_latest')
            or client.request('GET', 'releases/latest')['tag_name'] != 'latest'):
        client.request('PATCH', f'releases/{alias["id"]}', metadata)
    return {'tag': 'latest', 'plugin_tag': tag, 'published': True}
