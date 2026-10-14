"""Validate downloadable story pickers and their immutable release assets."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

CATALOG_NAMES = ('card-stories.json', 'event-stories.json', 'love-stories.json')
CATALOG_LIMIT = 1024 * 1024


def validate_catalog(name: str, data: bytes) -> dict:
    if name not in CATALOG_NAMES or not 0 < len(data) <= CATALOG_LIMIT:
        raise ValueError('Invalid story catalog name or size')
    value = json.loads(data.decode('utf-8'))
    if (not isinstance(value, dict) or type(value.get('schema_version')) is not int
            or value['schema_version'] != 1 or not isinstance(value.get('stories'), list)
            or len(value['stories']) > 8192):
        raise ValueError(f'{name}: invalid story catalog schema')
    kind = name.split('-')[0]
    group = 'card' if kind == 'card' else 'group'
    seen = set()
    for row in value['stories']:
        if not isinstance(row, dict):
            raise ValueError(f'{name}: invalid story record')
        file = row.get('file', '')
        if (not isinstance(file, str) or not re.fullmatch(r'adv_' + kind + r'_[a-z0-9_]+\.txt', file)
                or file.endswith('_short.txt') or file in seen):
            raise ValueError(f'{name}: invalid or duplicate story file')
        seen.add(file)
        if type(row.get('episode')) is not int or not 1 <= row['episode'] <= 20:
            raise ValueError(f'{name}: invalid episode')
        for key in ('title', group + '_id', group + '_name'):
            if (not isinstance(row.get(key), str) or '\0' in row[key]
                    or len(row[key]) > 8192 or (key.endswith('_id') and not row[key])):
                raise ValueError(f'{name}: invalid {key}')
        for key in ('display_label', 'character_id', 'character_name'):
            if key in row and (not isinstance(row[key], str) or '\0' in row[key] or len(row[key]) > 8192):
                raise ValueError(f'{name}: invalid {key}')
    return value


def release_catalog_assets(output: Path, repository: str | None = None,
                           tag: str | None = None) -> list[Path]:
    """Old releases have no catalogs; new releases must contain all three."""
    manifest = json.loads((output / 'manifest.json').read_text(encoding='utf-8'))
    if 'story_catalogs' not in manifest:
        return []
    entries = manifest['story_catalogs']
    if not isinstance(entries, dict) or set(entries) != set(CATALOG_NAMES):
        raise ValueError('Story catalog release must contain all three categories')
    result = []
    for name in CATALOG_NAMES:
        path = output / name
        if path.is_symlink():
            raise ValueError('Symlinked story catalog release asset')
        data = path.read_bytes()
        validate_catalog(name, data)
        if not isinstance(entries[name], dict) or entries[name].get('sha256') != hashlib.sha256(data).hexdigest():
            raise ValueError(f'Story catalog digest mismatch: {name}')
        if repository is not None:
            expected = f'https://github.com/{repository}/releases/download/{tag}/{name}'
            if entries[name].get('url') != expected:
                raise ValueError('Story catalog must reference the same immutable text release')
        result.append(path)
    return result
