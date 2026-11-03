"""Mark the verified plugin version as Latest in its source repository."""
from __future__ import annotations

from contextlib import contextmanager
import os
from pathlib import Path
import re

from release_followup import GitHub, PLUGIN_TAG
from release_latest import mark_latest, verified_assets

DEFAULT_REPOSITORY = 'DreamGallery/Idoly-localify'


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


def publish_latest_plugin(repository, tag, dry_run=False, *, apk=None, client=None):
    if not PLUGIN_TAG.fullmatch(tag):
        raise ValueError('Expected an explicit plugin version tag')
    if dry_run:
        return {'tag': tag, 'published': False}
    client = client or GitHub(repository)
    source = client.request('GET', f'releases/tags/{tag}')
    name = f'idoly-localify-{tag[1:]}.apk'
    assets = (source or {}).get('assets', [])
    if (not source or source.get('draft') or source.get('prerelease')
            or source.get('tag_name') != tag or len(assets) != 1
            or assets[0].get('name') != name or not assets[0].get('size')
            or not re.fullmatch(r'sha256:[0-9a-f]{64}', assets[0].get('digest') or '')):
        raise ValueError('Plugin version must contain one complete checksummed APK')
    if apk is not None and not verified_assets(source, [Path(apk)]):
        raise ValueError('Plugin APK differs from its published version; Latest unchanged')
    mark_latest(client, source)
    return {'tag': tag, 'published': True}
